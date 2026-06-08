// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _DEFAULT_SOURCE
#include "streaks_internal.h"

#include "../http/request.h"
#include "../http/response.h"
#include "../log.h"
#include "../redis/pipeline.h"
#include "../redis/redis.h"
#include "../redis/streak_update.h"
#include "../util.h"

#include <json-c/json.h>
#include <microhttpd.h>

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int64_t mig_iso_to_epoch_noon(const char *iso) {
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

int handle_migrate(struct http_request *r, void *userdata) {
	(void)userdata;
	char uid[MAX_UID_LEN];
	int err_rc = 0;
	if (require_bearer(r, uid, sizeof uid, &err_rc) != 0) return err_rc;

	struct json_object *body = http_req_json(r);
	if (!body || !json_object_is_type(body, json_type_object)) {
		if (body) json_object_put(body);
		return http_send_error(r->conn, 400, "Invalid payload: expected JSON object with recipient_id keys");
	}

	redisContext *c = redis_ctx_or_503(r->conn);
	if (!c) {
		json_object_put(body);
		return MHD_YES;
	}

	bool any_error = false;
	bool capped = false;
	size_t processed = 0;
	size_t imported = 0;
	size_t skipped = 0;

	json_object_object_foreach(body, recipient_id, data) {
		if (processed >= MAX_MIGRATE_RECIPIENTS) {
			log_warn("migrate: recipient cap exceeded (cap=%d uid=%s)", MAX_MIGRATE_RECIPIENTS, uid);
			capped = true;
			break;
		}
		processed++;
		if (!equi_is_discord_id(recipient_id)) {
			skipped++;
			continue;
		}
		if (strcmp(uid, recipient_id) == 0) {
			skipped++;
			continue;
		}
		if (!data || !json_object_is_type(data, json_type_object)) {
			skipped++;
			continue;
		}
		struct json_object *count_obj = NULL;
		if (!json_object_object_get_ex(data, "count", &count_obj)) {
			skipped++;
			continue;
		}
		if (!json_object_is_type(count_obj, json_type_int) &&
			!json_object_is_type(count_obj, json_type_double)) {
			skipped++;
			continue;
		}
		long legacy_count = (long)json_object_get_int64(count_obj);
		if (legacy_count < 0) {
			skipped++;
			continue;
		}

		const char *lo, *hi;
		sort_two_ids(uid, recipient_id, &lo, &hi);

		char key[256];
		if (redis_streak_key(lo, hi, key, sizeof key) < 0) {
			skipped++;
			continue;
		}

		redisReply *r1 = redisCommand(c, "HGET %b count", key, strlen(key));
		long existing = 0;
		if (r1 && r1->type == REDIS_REPLY_STRING) {
			char tmp[32];
			size_t l = (size_t)r1->len < sizeof tmp - 1 ? (size_t)r1->len : sizeof tmp - 1;
			memcpy(tmp, r1->str, l);
			tmp[l] = '\0';
			existing = strtol(tmp, NULL, 10);
		}
		redis_reply_safe_free(r1);

		long migrated = legacy_count > existing ? legacy_count : existing;
		if (migrated > 50) migrated = 50;
		if (migrated <= existing) {
			skipped++;
			continue;
		}

		struct json_object *flags_obj = NULL;
		int today_flags = 0;
		if (json_object_object_get_ex(data, "todayFlags", &flags_obj) && json_object_is_type(flags_obj, json_type_int))
			today_flags = json_object_get_int(flags_obj);

		bool is_user_a = (strcmp(uid, lo) == 0);
		bool today_bit = (today_flags & 1) != 0;
		bool user_a_today_b = is_user_a && today_bit;
		bool user_b_today_b = !is_user_a && today_bit;

		struct json_object *last_obj = NULL;
		const char *last_day = "";
		if (json_object_object_get_ex(data, "lastDay", &last_obj) && json_object_is_type(last_obj, json_type_string))
			last_day = json_object_get_string(last_obj);
		struct json_object *today_obj = NULL;
		const char *today_date_v = "";
		if (json_object_object_get_ex(data, "todayDate", &today_obj) && json_object_is_type(today_obj, json_type_string))
			today_date_v = json_object_get_string(today_obj);

		if (redis_streak_register_pair(c, lo, hi, key) != 0) {
			any_error = true;
			redis_invalidate_ctx();
			break;
		}

		int64_t now = (int64_t)time(NULL);
		int64_t last_round_ts = mig_iso_to_epoch_noon(last_day);
		int64_t pending_ts = mig_iso_to_epoch_noon(today_date_v);
		if (!pending_ts) pending_ts = now;
		enum streak_pending pending = STREAK_PENDING_NONE;
		if (user_a_today_b && !user_b_today_b)
			pending = STREAK_PENDING_A;
		else if (user_b_today_b && !user_a_today_b)
			pending = STREAK_PENDING_B;
		else
			pending_ts = 0;

		if (redis_streak_append_hmset(c, key, strlen(key), migrated,
									  lo, strlen(lo), hi, strlen(hi),
									  last_round_ts, pending, pending_ts, now) != 0) {
			any_error = true;
			redis_invalidate_ctx();
			break;
		}
		redisReply *rep = NULL;
		if (redisGetReply(c, (void **)&rep) != REDIS_OK) {
			any_error = true;
			redis_invalidate_ctx();
			break;
		}
		redis_reply_safe_free(rep);
		imported++;
	}

	json_object_put(body);

	if (any_error) return http_send_error(r->conn, 500, "Internal server error");

	char out_buf[256];
	int n = snprintf(out_buf, sizeof out_buf,
					 "{\"success\":true,\"processed\":%zu,\"imported\":%zu,\"skipped\":%zu,\"capped\":%s}",
					 processed, imported, skipped, capped ? "true" : "false");
	if (n < 0 || n >= (int)sizeof out_buf) return http_send_error(r->conn, 500, "Internal server error");
	return http_send_text(r->conn, 200, "application/json", out_buf, (size_t)n);
}
