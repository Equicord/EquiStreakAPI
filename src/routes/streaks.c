// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#include "streaks.h"

#include "../auth/cache.h"
#include "../auth/ratelimit.h"
#include "../config.h"
#include "../http/request.h"
#include "../http/response.h"
#include "../http/router.h"
#include "../log.h"
#include "../redis/pipeline.h"
#include "../redis/redis.h"
#include "../redis/streak_update.h"
#include "../util.h"
#include "streaks_internal.h"

#include <microhttpd.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int require_bearer(struct http_request *r, char *out_uid, size_t cap, int *err_rc) {
	*err_rc = 0;
	const char *h = http_req_header(r, "Authorization");
	if (!h || strncmp(h, "Bearer ", 7) != 0) {
		*err_rc = http_send_error(r->conn, 401, "Unauthorized: Invalid token format");
		return -1;
	}
	const char *token = h + 7;
	if (!equi_is_safe_token(token)) {
		*err_rc = http_send_error(r->conn, 401, "Unauthorized: Invalid token format");
		return -1;
	}
	if (auth_resolve_user(token, out_uid, cap) != 0) {
		*err_rc = http_send_error(r->conn, 401, "Unauthorized: Invalid token");
		return -1;
	}
	if (!equi_is_discord_id(out_uid)) {
		log_warn("auth: resolved user id failed shape check");
		*err_rc = http_send_error(r->conn, 401, "Unauthorized: Invalid token");
		return -1;
	}
	if (!ratelimit_check_ok(out_uid)) {
		*err_rc = http_send_too_many(r->conn, 60);
		return -1;
	}
	return 0;
}

void sort_two_ids(const char *a, const char *b, const char **lo, const char **hi) {
	if (strcmp(a, b) <= 0) {
		*lo = a;
		*hi = b;
	} else {
		*lo = b;
		*hi = a;
	}
}

static int handle_list(struct http_request *r, void *userdata) {
	(void)userdata;
	char uid[MAX_UID_LEN];
	int err_rc = 0;
	if (require_bearer(r, uid, sizeof uid, &err_rc) != 0) return err_rc;

	redisContext *c = redis_ctx_or_503(r->conn);
	if (!c) return MHD_YES;

	char uskey[128];
	int uslen = snprintf(uskey, sizeof uskey, "user_streaks:%s", uid);
	if (uslen < 0 || uslen >= (int)sizeof uskey)
		return http_send_error(r->conn, 400, "User ID too long");

	redisReply *members = redisCommand(c, "SMEMBERS %b", uskey, (size_t)uslen);
	if (!members) {
		redis_invalidate_ctx();
		return http_send_error(r->conn, 503, "Redis error");
	}
	if (members->type != REDIS_REPLY_ARRAY) {
		redis_reply_safe_free(members);
		return http_send_static(r->conn, 200, "application/json", "[]", 2);
	}

	struct equi_buf body = {0};
	equi_buf_grow(&body, 4096);
	equi_buf_putc(&body, '[');
	bool first = true;
	size_t appended = 0;
	for (size_t i = 0; i < members->elements; i++) {
		redisReply *m = members->element[i];
		if (m->type != REDIS_REPLY_STRING) continue;
		if (redisAppendCommand(c, "HGETALL %b", m->str, (size_t)m->len) != REDIS_OK) {
			log_warn("streaks list: pipeline append failed; invalidating ctx");
			redis_invalidate_ctx();
			redis_reply_safe_free(members);
			equi_buf_free(&body);
			return http_send_error(r->conn, 503, "Redis error");
		}
		appended++;
	}
	for (size_t i = 0; i < appended; i++) {
		redisReply *resp = NULL;
		if (redisGetReply(c, (void **)&resp) != REDIS_OK || !resp) {
			redis_reply_safe_free(resp);
			redis_invalidate_ctx();
			redis_reply_safe_free(members);
			equi_buf_free(&body);
			return http_send_error(r->conn, 503, "Redis error");
		}
		size_t mark = body.len;
		if (!first) equi_buf_putc(&body, ',');
		if (redis_reply_streak_append_json(resp, &body) == 0) {
			first = false;
		} else {
			body.len = mark;
			if (body.data) body.data[body.len] = '\0';
		}
		redis_reply_safe_free(resp);
	}
	redis_reply_safe_free(members);
	equi_buf_putc(&body, ']');
	return http_send_owned(r->conn, 200, "application/json", body.data, body.len);
}

static int handle_get_by_recipient(struct http_request *r, void *userdata) {
	(void)userdata;
	char uid[MAX_UID_LEN];
	int err_rc = 0;
	if (require_bearer(r, uid, sizeof uid, &err_rc) != 0) return err_rc;

	const char *recipient = http_req_param(r, "recipient_id");
	if (!equi_is_discord_id(recipient)) return http_send_error(r->conn, 400, "recipient_id must be a 1-20 digit Discord ID");

	const char *lo, *hi;
	sort_two_ids(uid, recipient, &lo, &hi);

	char key[256];
	if (redis_streak_key(lo, hi, key, sizeof key) < 0)
		return http_send_error(r->conn, 400, "User ID too long");

	redisContext *c = redis_ctx_or_503(r->conn);
	if (!c) return MHD_YES;

	redisReply *reply = redisCommand(c, "HGETALL %b", key, strlen(key));
	if (!reply) {
		redis_invalidate_ctx();
		return http_send_error(r->conn, 503, "Redis error");
	}
	if (reply->type != REDIS_REPLY_ARRAY || reply->elements == 0) {
		redis_reply_safe_free(reply);
		return http_send_error(r->conn, 404, "Streak not found");
	}
	struct equi_buf body = {0};
	int rc = redis_reply_streak_append_json(reply, &body);
	redis_reply_safe_free(reply);
	if (rc != 0) {
		equi_buf_free(&body);
		return http_send_error(r->conn, 500, "Invalid streak record");
	}
	return http_send_owned(r->conn, 200, "application/json", body.data, body.len);
}

static int handle_post_by_recipient(struct http_request *r, void *userdata) {
	(void)userdata;
	char uid[MAX_UID_LEN];
	int err_rc = 0;
	if (require_bearer(r, uid, sizeof uid, &err_rc) != 0) return err_rc;

	const char *recipient = http_req_param(r, "recipient_id");
	if (!equi_is_discord_id(recipient)) return http_send_error(r->conn, 400, "recipient_id must be a 1-20 digit Discord ID");
	if (strcmp(uid, recipient) == 0) return http_send_error(r->conn, 400, "Cannot streak with yourself");

	const char *lo, *hi;
	sort_two_ids(uid, recipient, &lo, &hi);

	char key[256];
	if (redis_streak_key(lo, hi, key, sizeof key) < 0)
		return http_send_error(r->conn, 400, "User ID too long");

	redisContext *c = redis_ctx_or_503(r->conn);
	if (!c) return MHD_YES;

	if (redis_streak_register_pair(c, lo, hi, key) != 0) {
		redis_invalidate_ctx();
		return http_send_error(r->conn, 503, "Redis error");
	}

	struct equi_buf body = {0};
	if (streak_update(uid, lo, hi, &body) != 0) {
		equi_buf_free(&body);
		return http_send_error(r->conn, 500, "Internal server error");
	}
	return http_send_owned(r->conn, 200, "application/json", body.data, body.len);
}

void routes_streaks_register(struct http_router *r, struct config *cfg) {
	router_add(r, "POST", "/api/streaks/admin/update", handle_admin_update, cfg);
	router_add(r, "POST", "/api/streaks/migrate", handle_migrate, cfg);
	router_add(r, "GET", "/api/streaks", handle_list, cfg);
	router_add(r, "GET", "/api/streaks/:recipient_id", handle_get_by_recipient, cfg);
	router_add(r, "POST", "/api/streaks/:recipient_id", handle_post_by_recipient, cfg);
}
