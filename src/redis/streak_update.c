// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _DEFAULT_SOURCE
#include "streak_update.h"

#include "../log.h"
#include "../util.h"
#include "pipeline.h"
#include "redis.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define STREAK_MAX_RETRIES 5

static int64_t iso_date_to_epoch_noon(const char *iso, size_t len);

static pthread_mutex_t timezone_mutex = PTHREAD_MUTEX_INITIALIZER;

static bool valid_timezone(const char *timezone) {
	if (!timezone || !*timezone || strlen(timezone) > 128 || timezone[0] == '/') return false;
	if (strcmp(timezone, "UTC") == 0 || strcmp(timezone, "Etc/UTC") == 0) return true;
	for (const unsigned char *p = (const unsigned char *)timezone; *p; p++)
		if (!((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
			  (*p >= '0' && *p <= '9') || *p == '_' || *p == '-' || *p == '+' || *p == '/')) return false;
	if (strstr(timezone, "..")) return false;
	char path[256];
	int n = snprintf(path, sizeof path, "/usr/share/zoneinfo/%s", timezone);
	return n > 0 && (size_t)n < sizeof path && access(path, R_OK) == 0;
}

static bool timezone_date(const char *timezone, int64_t now, char out[11]) {
	out[0] = '\0';
	if (!valid_timezone(timezone)) return false;
	pthread_mutex_lock(&timezone_mutex);
	const char *old = getenv("TZ");
	char old_copy[256];
	bool had_old = old != NULL;
	if (had_old) snprintf(old_copy, sizeof old_copy, "%s", old);
	int ok = setenv("TZ", timezone, 1);
	if (ok == 0) tzset();
	time_t t = (time_t)now;
	struct tm tm;
	if (ok == 0 && localtime_r(&t, &tm))
		strftime(out, 11, "%Y-%m-%d", &tm);
	else
		out[0] = '\0';
	if (had_old)
		setenv("TZ", old_copy, 1);
	else
		unsetenv("TZ");
	tzset();
	pthread_mutex_unlock(&timezone_mutex);
	return out[0] != '\0';
}

static bool date_is_more_than_one_day_old(const char *date, const char *today) {
	int64_t then = iso_date_to_epoch_noon(date, date ? strlen(date) : 0);
	int64_t now = iso_date_to_epoch_noon(today, today ? strlen(today) : 0);
	return then > 0 && now > then + 86400;
}

void streak_apply(int64_t now, int64_t window_s,
				  const char *current_uid, const char *message_author_id,
				  const char *user_a_id, const char *user_b_id,
				  const char *reported_timezone,
				  const struct streak_state *in, bool in_present,
				  struct streak_decision *out) {
	(void)window_s;
	struct streak_state s = {0};
	if (in_present && in) s = *in;
	if (valid_timezone(reported_timezone)) {
		if (strcmp(current_uid, user_a_id) == 0)
			snprintf(s.timezone_a, sizeof s.timezone_a, "%s", reported_timezone);
		else if (strcmp(current_uid, user_b_id) == 0)
			snprintf(s.timezone_b, sizeof s.timezone_b, "%s", reported_timezone);
	}
	const char *zone_a = s.timezone_a[0] ? s.timezone_a : (s.timezone_b[0] ? s.timezone_b : "UTC");
	const char *zone_b = s.timezone_b[0] ? s.timezone_b : (s.timezone_a[0] ? s.timezone_a : "UTC");
	if (!timezone_date(zone_a, now, s.today_a_date)) s.today_a_date[0] = '\0';
	if (!timezone_date(zone_b, now, s.today_b_date)) s.today_b_date[0] = '\0';
	if (s.activity_a_ts > 0)
		timezone_date(zone_a, s.activity_a_ts, s.activity_a_date);
	else
		s.activity_a_date[0] = '\0';
	if (s.activity_b_ts > 0)
		timezone_date(zone_b, s.activity_b_ts, s.activity_b_date);
	else
		s.activity_b_date[0] = '\0';

	if (message_author_id && strcmp(message_author_id, user_a_id) == 0)
		s.activity_a_ts = now;
	else if (message_author_id && strcmp(message_author_id, user_b_id) == 0)
		s.activity_b_ts = now;
	if (s.activity_a_ts > 0) timezone_date(zone_a, s.activity_a_ts, s.activity_a_date);
	if (s.activity_b_ts > 0) timezone_date(zone_b, s.activity_b_ts, s.activity_b_date);
	s.user_a_today = s.activity_a_date[0] && strcmp(s.activity_a_date, s.today_a_date) == 0;
	s.user_b_today = s.activity_b_date[0] && strcmp(s.activity_b_date, s.today_b_date) == 0;

	if ((s.completed_a_date[0] && date_is_more_than_one_day_old(s.completed_a_date, s.today_a_date)) ||
		(s.completed_b_date[0] && date_is_more_than_one_day_old(s.completed_b_date, s.today_b_date))) {
		s.count = 0;
		s.last_round_ts = 0;
		s.completed_a_date[0] = '\0';
		s.completed_b_date[0] = '\0';
		s.last_streak_date[0] = '\0';
	}
	bool first_completion = !s.completed_a_date[0] || !s.completed_b_date[0];
	bool both_days_advanced = !first_completion &&
							  strcmp(s.today_a_date, s.completed_a_date) > 0 &&
							  strcmp(s.today_b_date, s.completed_b_date) > 0;
	if (s.user_a_today && s.user_b_today && (first_completion || both_days_advanced)) {
		s.count += 1;
		s.last_round_ts = now;
		snprintf(s.completed_a_date, sizeof s.completed_a_date, "%s", s.today_a_date);
		snprintf(s.completed_b_date, sizeof s.completed_b_date, "%s", s.today_b_date);
	}
	if (s.completed_a_date[0]) snprintf(s.last_streak_date, sizeof s.last_streak_date, "%s", s.completed_a_date);
	s.pending = STREAK_PENDING_NONE;
	s.pending_ts = 0;
	s.pending_date[0] = '\0';
	out->wrote = true;
	out->state = s;
}

static bool parse_long(const char *s, long *out) {
	if (!s || !*s) return false;
	char *end = NULL;
	long v = strtol(s, &end, 10);
	if (!end || *end) return false;
	*out = v;
	return true;
}

static bool parse_int64(const char *s, int64_t *out) {
	if (!s || !*s) return false;
	char *end = NULL;
	long long v = strtoll(s, &end, 10);
	if (!end || *end) return false;
	*out = (int64_t)v;
	return true;
}

static int64_t iso_date_to_epoch_noon(const char *iso, size_t len) {
	if (!iso || len < 10) return 0;
	char buf[11];
	memcpy(buf, iso, 10);
	buf[10] = '\0';
	int y = 0, mo = 0, d = 0;
	if (sscanf(buf, "%4d-%2d-%2d", &y, &mo, &d) != 3) return 0;
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

static void copy_str(char *dst, size_t cap, const char *src, size_t n) {
	if (cap == 0) return;
	if (n >= cap) n = cap - 1;
	if (src && n) memcpy(dst, src, n);
	dst[n] = '\0';
}

int streak_update(const char *current_uid, const char *user_a_id, const char *user_b_id,
				  const char *message_author_id, const char *reported_timezone,
				  struct equi_buf *out) {
	char key[256];
	if (redis_streak_key(user_a_id, user_b_id, key, sizeof key) < 0) return -1;
	size_t key_len = strlen(key);

	int64_t now = (int64_t)time(NULL);

	redisContext *c = redis_ctx();
	if (!c) return -1;

	for (int attempt = 0; attempt < STREAK_MAX_RETRIES; attempt++) {
		if (redisAppendCommand(c, "WATCH %b", key, key_len) != REDIS_OK ||
			redisAppendCommand(c,
							   "HMGET %b count last_round_ts pending pending_ts "
							   "last_streak_date today_date user_a_today user_b_today timezone pending_date "
							   "timezone_a timezone_b activity_a_ts activity_b_ts activity_a_date activity_b_date "
							   "today_a_date today_b_date completed_a_date completed_b_date",
							   key, key_len) != REDIS_OK) {
			redis_invalidate_ctx();
			return -1;
		}
		redisReply *r = NULL;
		if (redisGetReply(c, (void **)&r) != REDIS_OK) {
			redis_reply_safe_free(r);
			redis_invalidate_ctx();
			return -1;
		}
		redis_reply_safe_free(r);
		r = NULL;
		if (redisGetReply(c, (void **)&r) != REDIS_OK) {
			redis_reply_safe_free(r);
			redis_invalidate_ctx();
			return -1;
		}
		if (!r || r->type != REDIS_REPLY_ARRAY || r->elements != 20) {
			redis_reply_safe_free(r);
			redis_reply_safe_free(redisCommand(c, "UNWATCH"));
			redis_invalidate_ctx();
			return -1;
		}

		struct streak_state in_state = {0};
		bool in_present = false;
		bool has_new_schema = false;
		char legacy_timezone[129] = "";
		if (r->element[8]->type == REDIS_REPLY_STRING && valid_timezone(r->element[8]->str))
			snprintf(legacy_timezone, sizeof legacy_timezone, "%s", r->element[8]->str);

		for (size_t i = 0; i < r->elements; i++)
			if (r->element[i]->type == REDIS_REPLY_STRING) in_present = true;

		if (in_present) {
			char tmp[64];
			in_state.user_a_today = r->element[6]->type == REDIS_REPLY_STRING &&
									r->element[6]->len == 1 && r->element[6]->str[0] == '1';
			in_state.user_b_today = r->element[7]->type == REDIS_REPLY_STRING &&
									r->element[7]->len == 1 && r->element[7]->str[0] == '1';

			if (r->element[0]->type == REDIS_REPLY_STRING && r->element[0]->len < sizeof tmp) {
				copy_str(tmp, sizeof tmp, r->element[0]->str, (size_t)r->element[0]->len);
				long cv = 0;
				if (parse_long(tmp, &cv)) in_state.count = cv;
			}

			if (r->element[1]->type == REDIS_REPLY_STRING && r->element[1]->len < sizeof tmp) {
				copy_str(tmp, sizeof tmp, r->element[1]->str, (size_t)r->element[1]->len);
				int64_t v = 0;
				if (parse_int64(tmp, &v)) {
					in_state.last_round_ts = v;
					has_new_schema = true;
				}
			}

			if (r->element[2]->type == REDIS_REPLY_STRING && r->element[2]->len == 1) {
				if (r->element[2]->str[0] == 'a')
					in_state.pending = STREAK_PENDING_A;
				else if (r->element[2]->str[0] == 'b')
					in_state.pending = STREAK_PENDING_B;
				has_new_schema = true;
			}

			if (r->element[3]->type == REDIS_REPLY_STRING && r->element[3]->len < sizeof tmp) {
				copy_str(tmp, sizeof tmp, r->element[3]->str, (size_t)r->element[3]->len);
				int64_t v = 0;
				if (parse_int64(tmp, &v)) in_state.pending_ts = v;
			}

			if (!has_new_schema) {
				int64_t lsd_epoch = 0;
				if (r->element[4]->type == REDIS_REPLY_STRING && r->element[4]->len >= 10)
					lsd_epoch = iso_date_to_epoch_noon(r->element[4]->str, (size_t)r->element[4]->len);
				int64_t td_epoch = 0;
				if (r->element[5]->type == REDIS_REPLY_STRING && r->element[5]->len >= 10)
					td_epoch = iso_date_to_epoch_noon(r->element[5]->str, (size_t)r->element[5]->len);
				bool a_today = (r->element[6]->type == REDIS_REPLY_STRING &&
								r->element[6]->len == 1 && r->element[6]->str[0] == '1');
				bool b_today = (r->element[7]->type == REDIS_REPLY_STRING &&
								r->element[7]->len == 1 && r->element[7]->str[0] == '1');

				in_state.last_round_ts = lsd_epoch ? lsd_epoch + 86400 / 2 : 0;
				if (a_today && !b_today) {
					in_state.pending = STREAK_PENDING_A;
					in_state.pending_ts = td_epoch ? td_epoch : now;
				} else if (b_today && !a_today) {
					in_state.pending = STREAK_PENDING_B;
					in_state.pending_ts = td_epoch ? td_epoch : now;
				} else {
					in_state.pending = STREAK_PENDING_NONE;
					in_state.pending_ts = 0;
				}
			}
			if (r->element[4]->type == REDIS_REPLY_STRING && r->element[4]->len >= 10)
				copy_str(in_state.last_streak_date, sizeof in_state.last_streak_date, r->element[4]->str, 10);
			if (r->element[9]->type == REDIS_REPLY_STRING && r->element[9]->len >= 10)
				copy_str(in_state.pending_date, sizeof in_state.pending_date, r->element[9]->str, 10);
			else if (in_state.pending != STREAK_PENDING_NONE && r->element[5]->type == REDIS_REPLY_STRING && r->element[5]->len >= 10)
				copy_str(in_state.pending_date, sizeof in_state.pending_date, r->element[5]->str, 10);

			if (r->element[10]->type == REDIS_REPLY_STRING && valid_timezone(r->element[10]->str))
				copy_str(in_state.timezone_a, sizeof in_state.timezone_a, r->element[10]->str, (size_t)r->element[10]->len);
			if (r->element[11]->type == REDIS_REPLY_STRING && valid_timezone(r->element[11]->str))
				copy_str(in_state.timezone_b, sizeof in_state.timezone_b, r->element[11]->str, (size_t)r->element[11]->len);
			if (!in_state.timezone_a[0] && !in_state.timezone_b[0] && legacy_timezone[0]) {
				snprintf(in_state.timezone_a, sizeof in_state.timezone_a, "%s", legacy_timezone);
				snprintf(in_state.timezone_b, sizeof in_state.timezone_b, "%s", legacy_timezone);
			}
			if (r->element[12]->type == REDIS_REPLY_STRING && r->element[12]->len < 32) {
				copy_str(tmp, sizeof tmp, r->element[12]->str, (size_t)r->element[12]->len);
				parse_int64(tmp, &in_state.activity_a_ts);
			}
			if (r->element[13]->type == REDIS_REPLY_STRING && r->element[13]->len < 32) {
				copy_str(tmp, sizeof tmp, r->element[13]->str, (size_t)r->element[13]->len);
				parse_int64(tmp, &in_state.activity_b_ts);
			}
			if (r->element[14]->type == REDIS_REPLY_STRING && r->element[14]->len >= 10)
				copy_str(in_state.activity_a_date, sizeof in_state.activity_a_date, r->element[14]->str, 10);
			if (r->element[15]->type == REDIS_REPLY_STRING && r->element[15]->len >= 10)
				copy_str(in_state.activity_b_date, sizeof in_state.activity_b_date, r->element[15]->str, 10);
			if (r->element[18]->type == REDIS_REPLY_STRING && r->element[18]->len >= 10)
				copy_str(in_state.completed_a_date, sizeof in_state.completed_a_date, r->element[18]->str, 10);
			if (r->element[19]->type == REDIS_REPLY_STRING && r->element[19]->len >= 10)
				copy_str(in_state.completed_b_date, sizeof in_state.completed_b_date, r->element[19]->str, 10);
			if (!in_state.completed_a_date[0] && in_state.last_streak_date[0]) {
				snprintf(in_state.completed_a_date, sizeof in_state.completed_a_date, "%s", in_state.last_streak_date);
				snprintf(in_state.completed_b_date, sizeof in_state.completed_b_date, "%s", in_state.last_streak_date);
			}
			if (!in_state.activity_a_ts && !in_state.activity_b_ts && in_state.last_round_ts > 0) {
				in_state.activity_a_ts = in_state.last_round_ts;
				in_state.activity_b_ts = in_state.last_round_ts;
			}
			if (in_state.pending == STREAK_PENDING_A && in_state.pending_ts > in_state.activity_a_ts)
				in_state.activity_a_ts = in_state.pending_ts;
			if (in_state.pending == STREAK_PENDING_B && in_state.pending_ts > in_state.activity_b_ts)
				in_state.activity_b_ts = in_state.pending_ts;
		}
		redis_reply_safe_free(r);

		struct streak_decision dec;
		streak_apply(now, STREAK_WINDOW_S_DEFAULT, current_uid, message_author_id,
					 user_a_id, user_b_id, reported_timezone,
					 &in_state, in_present, &dec);

		if (!dec.wrote) {
			if (redisAppendCommand(c, "UNWATCH") != REDIS_OK ||
				redisAppendCommand(c, "HGETALL %b", key, key_len) != REDIS_OK) {
				redis_invalidate_ctx();
				return -1;
			}
			redisReply *unw = NULL;
			redisReply *got = NULL;
			if (redisGetReply(c, (void **)&unw) != REDIS_OK) {
				redis_reply_safe_free(unw);
				redis_invalidate_ctx();
				return -1;
			}
			redis_reply_safe_free(unw);
			if (redisGetReply(c, (void **)&got) != REDIS_OK) {
				redis_reply_safe_free(got);
				redis_invalidate_ctx();
				return -1;
			}
			int rc = redis_reply_streak_append_json(got, out);
			redis_reply_safe_free(got);
			return rc;
		}

		if (redisAppendCommand(c, "MULTI") != REDIS_OK ||
			redis_streak_append_hmset(c, key, key_len,
									  dec.state.count,
									  user_a_id, strlen(user_a_id),
									  user_b_id, strlen(user_b_id),
									  dec.state.last_round_ts,
									  dec.state.pending,
									  dec.state.pending_ts,
									  dec.state.last_streak_date,
									  dec.state.today_a_date,
									  dec.state.pending_date,
									  dec.state.timezone_a[0] ? dec.state.timezone_a : dec.state.timezone_b,
									  dec.state.timezone_b[0] ? dec.state.timezone_b : dec.state.timezone_a,
									  now) != 0 ||
			redis_streak_append_activity_hmset(c, key, key_len, &dec.state) != 0 ||
			redisAppendCommand(c, "HGETALL %b", key, key_len) != REDIS_OK ||
			redisAppendCommand(c, "EXEC") != REDIS_OK) {
			redis_invalidate_ctx();
			return -1;
		}
		redisReply *replies[5] = {0};
		bool all_ok = true;
		for (int i = 0; i < 5; i++) {
			if (redisGetReply(c, (void **)&replies[i]) != REDIS_OK) {
				all_ok = false;
				break;
			}
		}
		if (!all_ok || !replies[4]) {
			for (int i = 0; i < 5; i++)
				redis_reply_safe_free(replies[i]);
			redis_invalidate_ctx();
			return -1;
		}
		redis_reply_safe_free(replies[0]);
		redis_reply_safe_free(replies[1]);
		redis_reply_safe_free(replies[2]);
		redis_reply_safe_free(replies[3]);
		r = replies[4];
		if (r->type == REDIS_REPLY_NIL) {
			redis_reply_safe_free(r);
			continue;
		}
		if (r->type != REDIS_REPLY_ARRAY || r->elements < 2) {
			redis_reply_safe_free(r);
			return -1;
		}
		int rc = redis_reply_streak_append_json(r->element[2], out);
		redis_reply_safe_free(r);
		return rc;
	}

	log_warn("streak_update: exhausted retries for %s", key);
	return -1;
}
