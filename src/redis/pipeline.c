// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#include "pipeline.h"

#include "../util.h"
#include "redis.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int redis_streak_key(const char *lo, const char *hi, char *buf, size_t cap) {
	int n = snprintf(buf, cap, "streak:%s:%s", lo, hi);
	if (n < 0 || (size_t)n >= cap) return -1;
	return n;
}

int redis_streak_append_hmset(redisContext *c,
							  const char *key, size_t key_len,
							  long count,
							  const char *lo, size_t lo_len,
							  const char *hi, size_t hi_len,
							  const char *last_streak_date,
							  const char *today_date,
							  bool user_a_today, bool user_b_today) {
	if (!c || !key || !lo || !hi) return -1;
	char count_buf[32];
	snprintf(count_buf, sizeof count_buf, "%ld", count);
	return redisAppendCommand(c,
							  "HMSET %b "
							  "count %s "
							  "user_a_id %b "
							  "user_b_id %b "
							  "last_streak_date %s "
							  "today_date %s "
							  "user_a_today %s "
							  "user_b_today %s",
							  key, key_len,
							  count_buf,
							  lo, lo_len,
							  hi, hi_len,
							  last_streak_date ? last_streak_date : "",
							  today_date ? today_date : "",
							  user_a_today ? "1" : "0",
							  user_b_today ? "1" : "0") == REDIS_OK
			   ? 0
			   : -1;
}

int redis_streak_register_pair(redisContext *c, const char *lo, const char *hi, const char *key) {
	if (!c || !lo || !hi || !key) return -1;

	char lo_key[128], hi_key[128];
	int lo_n = snprintf(lo_key, sizeof lo_key, "user_streaks:%s", lo);
	int hi_n = snprintf(hi_key, sizeof hi_key, "user_streaks:%s", hi);
	if (lo_n < 0 || lo_n >= (int)sizeof lo_key || hi_n < 0 || hi_n >= (int)sizeof hi_key) return -1;

	size_t key_len = strlen(key);
	if (redisAppendCommand(c, "SADD %b %b", lo_key, (size_t)lo_n, key, key_len) != REDIS_OK) return -1;
	if (redisAppendCommand(c, "SADD %b %b", hi_key, (size_t)hi_n, key, key_len) != REDIS_OK) return -1;

	int rc = 0;
	for (int i = 0; i < 2; i++) {
		redisReply *r = NULL;
		if (redisGetReply(c, (void **)&r) != REDIS_OK) {
			redis_reply_safe_free(r);
			rc = -1;
			continue;
		}
		if (!r || r->type == REDIS_REPLY_ERROR) rc = -1;
		redis_reply_safe_free(r);
	}
	return rc;
}

int redis_reply_streak_append_json(redisReply *r, struct equi_buf *out) {
	if (!r || r->type != REDIS_REPLY_ARRAY || r->elements == 0 || r->elements % 2 != 0)
		return -1;

	const char *user_a_id = NULL, *user_b_id = NULL;
	size_t ua_len = 0, ub_len = 0;
	const char *count = NULL;
	size_t count_len = 0;
	const char *last_streak_date = NULL;
	size_t lsd_len = 0;
	const char *user_a_today = NULL;
	size_t uat_len = 0;
	const char *user_b_today = NULL;
	size_t ubt_len = 0;
	const char *today_date = NULL;
	size_t td_len = 0;

	for (size_t i = 0; i + 1 < r->elements; i += 2) {
		redisReply *k = r->element[i];
		redisReply *v = r->element[i + 1];
		if (k->type != REDIS_REPLY_STRING || v->type != REDIS_REPLY_STRING) continue;
		if (k->len == 9 && memcmp(k->str, "user_a_id", 9) == 0) {
			user_a_id = v->str;
			ua_len = (size_t)v->len;
		} else if (k->len == 9 && memcmp(k->str, "user_b_id", 9) == 0) {
			user_b_id = v->str;
			ub_len = (size_t)v->len;
		} else if (k->len == 5 && memcmp(k->str, "count", 5) == 0) {
			count = v->str;
			count_len = (size_t)v->len;
		} else if (k->len == 16 && memcmp(k->str, "last_streak_date", 16) == 0) {
			last_streak_date = v->str;
			lsd_len = (size_t)v->len;
		} else if (k->len == 12 && memcmp(k->str, "user_a_today", 12) == 0) {
			user_a_today = v->str;
			uat_len = (size_t)v->len;
		} else if (k->len == 12 && memcmp(k->str, "user_b_today", 12) == 0) {
			user_b_today = v->str;
			ubt_len = (size_t)v->len;
		} else if (k->len == 10 && memcmp(k->str, "today_date", 10) == 0) {
			today_date = v->str;
			td_len = (size_t)v->len;
		}
	}

	if (!user_a_id || !user_b_id) return -1;

	long count_v = 0;
	if (count && count_len < 32) {
		char cnt[32];
		memcpy(cnt, count, count_len);
		cnt[count_len] = '\0';
		count_v = strtol(cnt, NULL, 10);
	}

	bool a_today = (uat_len == 1 && user_a_today && user_a_today[0] == '1');
	bool b_today = (ubt_len == 1 && user_b_today && user_b_today[0] == '1');

	int rc = 0;
	rc |= equi_buf_puts(out, "{\"id\":\"");
	rc |= equi_buf_putn(out, user_a_id, ua_len);
	rc |= equi_buf_putc(out, ':');
	rc |= equi_buf_putn(out, user_b_id, ub_len);
	rc |= equi_buf_puts(out, "\",\"user_a_id\":\"");
	rc |= equi_buf_putn(out, user_a_id, ua_len);
	rc |= equi_buf_puts(out, "\",\"user_b_id\":\"");
	rc |= equi_buf_putn(out, user_b_id, ub_len);
	rc |= equi_buf_puts(out, "\",\"count\":");
	rc |= equi_buf_printf(out, "%ld", count_v);
	rc |= equi_buf_puts(out, ",\"last_streak_date\":");
	if (last_streak_date && lsd_len > 0) {
		rc |= equi_buf_putc(out, '"');
		rc |= equi_buf_putn(out, last_streak_date, lsd_len);
		rc |= equi_buf_putc(out, '"');
	} else {
		rc |= equi_buf_puts(out, "null");
	}
	rc |= equi_buf_puts(out, ",\"user_a_today\":");
	rc |= equi_buf_puts(out, a_today ? "true" : "false");
	rc |= equi_buf_puts(out, ",\"user_b_today\":");
	rc |= equi_buf_puts(out, b_today ? "true" : "false");
	rc |= equi_buf_puts(out, ",\"today_date\":");
	if (today_date && td_len > 0) {
		rc |= equi_buf_putc(out, '"');
		rc |= equi_buf_putn(out, today_date, td_len);
		rc |= equi_buf_putc(out, '"');
	} else if (today_date) {
		rc |= equi_buf_puts(out, "\"\"");
	} else {
		rc |= equi_buf_puts(out, "null");
	}
	rc |= equi_buf_putc(out, '}');
	return rc;
}
