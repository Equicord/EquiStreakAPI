// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

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

static void copy_iso(char dst[16], const char *src, size_t len) {
	size_t n = len < 15 ? len : 15;
	if (src && n) memcpy(dst, src, n);
	dst[n] = '\0';
}

void streak_apply(const char *today_iso, const char *yesterday_iso,
				  const char *current_uid, const char *user_a_id, const char *user_b_id,
				  const struct streak_state *in, bool in_present,
				  struct streak_decision *out) {
	(void)user_b_id;
	struct streak_state s;
	if (in_present && in) {
		s = *in;
	} else {
		s.count = 0;
		s.last_streak_date[0] = '\0';
		s.today_date[0] = '\0';
		s.user_a_today = false;
		s.user_b_today = false;
	}

	if (strcmp(s.today_date, today_iso) != 0) {
		if (strcmp(s.today_date, yesterday_iso) == 0) {
			if (!s.user_a_today || !s.user_b_today) {
				s.count = 0;
			}
		} else if (s.today_date[0]) {
			s.count = 0;
		}
		s.user_a_today = false;
		s.user_b_today = false;
		copy_iso(s.today_date, today_iso, strlen(today_iso));
	}

	bool is_user_a = (strcmp(current_uid, user_a_id) == 0);

	if ((is_user_a && s.user_a_today) || (!is_user_a && s.user_b_today)) {
		out->wrote = false;
		out->state = s;
		return;
	}

	if (is_user_a)
		s.user_a_today = true;
	else
		s.user_b_today = true;

	if (s.user_a_today && s.user_b_today && strcmp(s.last_streak_date, today_iso) != 0) {
		s.count += 1;
		copy_iso(s.last_streak_date, today_iso, strlen(today_iso));
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

int streak_update(const char *current_uid, const char *user_a_id, const char *user_b_id,
				  struct equi_buf *out) {
	char key[256];
	if (redis_streak_key(user_a_id, user_b_id, key, sizeof key) < 0) return -1;
	size_t key_len = strlen(key);

	char today[11];
	char yesterday[11];
	time_t now = time(NULL);
	equi_iso_from_epoch(now, today);
	equi_iso_from_epoch(now - 86400, yesterday);

	redisContext *c = redis_ctx();
	if (!c) return -1;

	for (int attempt = 0; attempt < STREAK_MAX_RETRIES; attempt++) {
		if (redisAppendCommand(c, "WATCH %b", key, key_len) != REDIS_OK ||
			redisAppendCommand(c,
							   "HMGET %b count last_streak_date user_a_today user_b_today today_date",
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
		if (!r || r->type != REDIS_REPLY_ARRAY || r->elements != 5) {
			redis_reply_safe_free(r);
			redis_reply_safe_free(redisCommand(c, "UNWATCH"));
			redis_invalidate_ctx();
			return -1;
		}

		struct streak_state in = {0};
		bool in_present = false;
		for (size_t i = 0; i < 5; i++) {
			redisReply *e = r->element[i];
			if (e->type == REDIS_REPLY_STRING) in_present = true;
		}
		if (in_present) {
			if (r->element[0]->type == REDIS_REPLY_STRING && r->element[0]->len < 32) {
				char tmp[32];
				memcpy(tmp, r->element[0]->str, (size_t)r->element[0]->len);
				tmp[r->element[0]->len] = '\0';
				long cv = 0;
				if (parse_long(tmp, &cv)) in.count = cv;
			}
			if (r->element[1]->type == REDIS_REPLY_STRING)
				copy_iso(in.last_streak_date, r->element[1]->str, (size_t)r->element[1]->len);
			if (r->element[2]->type == REDIS_REPLY_STRING)
				in.user_a_today = (r->element[2]->len == 1 && r->element[2]->str[0] == '1');
			if (r->element[3]->type == REDIS_REPLY_STRING)
				in.user_b_today = (r->element[3]->len == 1 && r->element[3]->str[0] == '1');
			if (r->element[4]->type == REDIS_REPLY_STRING)
				copy_iso(in.today_date, r->element[4]->str, (size_t)r->element[4]->len);
		}
		redis_reply_safe_free(r);

		struct streak_decision dec;
		streak_apply(today, yesterday, current_uid, user_a_id, user_b_id, &in, in_present, &dec);

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
			redis_streak_append_hmset(c, key, key_len, dec.state.count,
									  user_a_id, strlen(user_a_id),
									  user_b_id, strlen(user_b_id),
									  dec.state.last_streak_date,
									  dec.state.today_date,
									  dec.state.user_a_today, dec.state.user_b_today) != 0 ||
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
