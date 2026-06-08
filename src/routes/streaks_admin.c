// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _DEFAULT_SOURCE
#include "streaks_internal.h"

#include "../auth/admin.h"
#include "../config.h"
#include "../http/request.h"
#include "../http/response.h"
#include "../log.h"
#include "../redis/pipeline.h"
#include "../redis/redis.h"
#include "../redis/streak_update.h"
#include "../util.h"

#include <json-c/json.h>
#include <microhttpd.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int64_t admin_iso_to_epoch_noon(const char *iso) {
	if (!iso || strlen(iso) < 10) return 0;
	int y = 0, mo = 0, d = 0;
	if (sscanf(iso, "%4d-%2d-%2d", &y, &mo, &d) != 3) return 0;
	if (y < 1970 || mo < 1 || mo > 12 || d < 1 || d > 31) return 0;
	struct tm t;
	memset(&t, 0, sizeof t);
	t.tm_year = y - 1900;
	t.tm_mon = mo - 1;
	t.tm_mday = d;
	t.tm_hour = 12;
	time_t e = timegm(&t);
	return (e == (time_t)-1) ? 0 : (int64_t)e;
}

int handle_admin_update(struct http_request *r, void *userdata) {
	struct config *cfg = userdata;
	if (!admin_check(r, cfg)) {
		if (!cfg->master_api_key || !cfg->master_api_key[0])
			return http_send_error(r->conn, 500, "Server misconfiguration: MASTER_API_KEY not set");
		return http_send_error(r->conn, 403, "Forbidden: Invalid API Key");
	}

	struct json_object *body = http_req_json(r);
	if (!body || !json_object_is_type(body, json_type_object))
		return http_send_error(r->conn, 400, "Missing required fields: user_a_id, user_b_id, count");

	const char *a = NULL, *b = NULL;
	int64_t count_v64 = 0;
	if (!j_get_str(body, "user_a_id", &a)) {
		json_object_put(body);
		return http_send_error(r->conn, 400, "Missing or non-string field: user_a_id");
	}
	if (!j_get_str(body, "user_b_id", &b)) {
		json_object_put(body);
		return http_send_error(r->conn, 400, "Missing or non-string field: user_b_id");
	}
	if (!j_get_int64(body, "count", &count_v64)) {
		json_object_put(body);
		return http_send_error(r->conn, 400, "Missing or non-numeric field: count");
	}
	long count_v = (long)count_v64;

	if (!equi_is_discord_id(a)) {
		json_object_put(body);
		return http_send_error(r->conn, 400, "user_a_id must be a 1-20 digit Discord ID");
	}
	if (!equi_is_discord_id(b)) {
		json_object_put(body);
		return http_send_error(r->conn, 400, "user_b_id must be a 1-20 digit Discord ID");
	}
	if (strcmp(a, b) == 0) {
		json_object_put(body);
		return http_send_error(r->conn, 400, "user_a_id and user_b_id must differ");
	}
	if (count_v < 0) count_v = 0;

	const char *lo, *hi;
	sort_two_ids(a, b, &lo, &hi);

	log_info("audit admin_update req_id=%s ip=%s streak=%s:%s count=%ld",
			 r->req_id, http_req_client_ip(r), lo, hi, count_v);

	int64_t now = (int64_t)time(NULL);

	char today[11];
	equi_today_iso(today);

	const char *last_streak_date = today;
	const char *today_date_v = today;
	bool user_a_today_v = false;
	bool user_b_today_v = false;

	const char *s_opt = NULL;
	if (j_get_str(body, "last_streak_date", &s_opt)) last_streak_date = s_opt;
	if (j_get_str(body, "today_date", &s_opt)) today_date_v = s_opt;
	j_get_bool(body, "user_a_today", &user_a_today_v);
	j_get_bool(body, "user_b_today", &user_b_today_v);

	int64_t last_round_ts = admin_iso_to_epoch_noon(last_streak_date);
	int64_t pending_ts = admin_iso_to_epoch_noon(today_date_v);
	if (!pending_ts) pending_ts = now;
	enum streak_pending pending = STREAK_PENDING_NONE;
	if (user_a_today_v && !user_b_today_v)
		pending = STREAK_PENDING_A;
	else if (user_b_today_v && !user_a_today_v)
		pending = STREAK_PENDING_B;
	else
		pending_ts = 0;

	char key[256];
	if (redis_streak_key(lo, hi, key, sizeof key) < 0) {
		json_object_put(body);
		return http_send_error(r->conn, 400, "User ID too long");
	}

	redisContext *c = redis_ctx_or_503(r->conn);
	if (!c) {
		json_object_put(body);
		return MHD_YES;
	}

	if (redis_streak_register_pair(c, lo, hi, key) != 0) {
		json_object_put(body);
		redis_invalidate_ctx();
		return http_send_error(r->conn, 503, "Redis error");
	}

	size_t key_len = strlen(key);
	if (redis_streak_append_hmset(c, key, key_len, count_v,
								  lo, strlen(lo), hi, strlen(hi),
								  last_round_ts, pending, pending_ts, now) != 0 ||
		redisAppendCommand(c, "HGETALL %b", key, key_len) != REDIS_OK) {
		json_object_put(body);
		redis_invalidate_ctx();
		return http_send_error(r->conn, 500, "Internal server error");
	}
	redisReply *hset = NULL;
	redisReply *all = NULL;
	if (redisGetReply(c, (void **)&hset) != REDIS_OK ||
		redisGetReply(c, (void **)&all) != REDIS_OK) {
		redis_reply_safe_free(hset);
		redis_reply_safe_free(all);
		json_object_put(body);
		redis_invalidate_ctx();
		return http_send_error(r->conn, 500, "Internal server error");
	}
	redis_reply_safe_free(hset);
	json_object_put(body);
	struct equi_buf out = {0};
	int rc = redis_reply_streak_append_json(all, &out);
	redis_reply_safe_free(all);
	if (rc != 0) {
		equi_buf_free(&out);
		return http_send_error(r->conn, 500, "Internal server error");
	}
	return http_send_owned(r->conn, 200, "application/json", out.data, out.len);
}
