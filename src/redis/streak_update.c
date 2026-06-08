// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _DEFAULT_SOURCE
#include "streak_update.h"

#include "../log.h"
#include "../util.h"
#include "pipeline.h"
#include "redis.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define STREAK_MAX_RETRIES 5

void streak_apply(int64_t now, int64_t window_s,
				  const char *current_uid,
				  const char *user_a_id, const char *user_b_id,
				  const struct streak_state *in, bool in_present,
				  struct streak_decision *out) {
	(void)user_b_id;
	struct streak_state s;
	if (in_present && in) {
		s = *in;
	} else {
		s.count = 0;
		s.last_round_ts = 0;
		s.pending_ts = 0;
		s.pending = STREAK_PENDING_NONE;
	}

	if (window_s <= 0) window_s = STREAK_WINDOW_S_DEFAULT;

	if (s.last_round_ts > 0 && now - s.last_round_ts > window_s) {
		s.count = 0;
		s.last_round_ts = 0;
		s.pending = STREAK_PENDING_NONE;
		s.pending_ts = 0;
	}

	bool is_a = (strcmp(current_uid, user_a_id) == 0);
	enum streak_pending me = is_a ? STREAK_PENDING_A : STREAK_PENDING_B;

	bool pending_me_fresh = (s.pending == me) && (now - s.pending_ts <= window_s);
	if (pending_me_fresh) {
		out->wrote = false;
		out->state = s;
		return;
	}

	bool pending_other_fresh =
		(s.pending != STREAK_PENDING_NONE) && (s.pending != me) && (now - s.pending_ts <= window_s);

	if (pending_other_fresh) {
		s.count += 1;
		s.last_round_ts = now;
		s.pending = STREAK_PENDING_NONE;
		s.pending_ts = 0;
	} else {
		s.pending = me;
		s.pending_ts = now;
	}

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
							   "last_streak_date today_date user_a_today user_b_today",
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
		if (!r || r->type != REDIS_REPLY_ARRAY || r->elements != 8) {
			redis_reply_safe_free(r);
			redis_reply_safe_free(redisCommand(c, "UNWATCH"));
			redis_invalidate_ctx();
			return -1;
		}

		struct streak_state in_state = {0};
		bool in_present = false;
		bool has_new_schema = false;

		for (size_t i = 0; i < r->elements; i++)
			if (r->element[i]->type == REDIS_REPLY_STRING) in_present = true;

		if (in_present) {
			char tmp[64];

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
		}
		redis_reply_safe_free(r);

		struct streak_decision dec;
		streak_apply(now, STREAK_WINDOW_S_DEFAULT, current_uid, user_a_id, user_b_id,
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
									  now) != 0 ||
			redisAppendCommand(c, "HGETALL %b", key, key_len) != REDIS_OK ||
			redisAppendCommand(c, "EXEC") != REDIS_OK) {
			redis_invalidate_ctx();
			return -1;
		}
		redisReply *replies[4] = {0};
		bool all_ok = true;
		for (int i = 0; i < 4; i++) {
			if (redisGetReply(c, (void **)&replies[i]) != REDIS_OK) {
				all_ok = false;
				break;
			}
		}
		if (!all_ok || !replies[3]) {
			for (int i = 0; i < 4; i++)
				redis_reply_safe_free(replies[i]);
			redis_invalidate_ctx();
			return -1;
		}
		redis_reply_safe_free(replies[0]);
		redis_reply_safe_free(replies[1]);
		redis_reply_safe_free(replies[2]);
		r = replies[3];
		if (r->type == REDIS_REPLY_NIL) {
			redis_reply_safe_free(r);
			continue;
		}
		if (r->type != REDIS_REPLY_ARRAY || r->elements < 2) {
			redis_reply_safe_free(r);
			return -1;
		}
		int rc = redis_reply_streak_append_json(r->element[1], out);
		redis_reply_safe_free(r);
		return rc;
	}

	log_warn("streak_update: exhausted retries for %s", key);
	return -1;
}
