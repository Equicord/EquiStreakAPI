// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _DEFAULT_SOURCE
#include "pipeline.h"

#include "../util.h"
#include "redis.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int redis_streak_key(const char *lo, const char *hi, char *buf, size_t cap) {
	int n = snprintf(buf, cap, "streak:%s:%s", lo, hi);
	if (n < 0 || (size_t)n >= cap) return -1;
	return n;
}

static void epoch_to_iso(int64_t epoch, char out[11]) {
	if (epoch <= 0) {
		out[0] = '\0';
		return;
	}
	time_t t = (time_t)epoch;
	struct tm tm;
	gmtime_r(&t, &tm);
	strftime(out, 11, "%Y-%m-%d", &tm);
}

int redis_streak_append_hmset(redisContext *c,
							  const char *key, size_t key_len,
							  long count,
							  const char *lo, size_t lo_len,
							  const char *hi, size_t hi_len,
							  int64_t last_round_ts,
							  enum streak_pending pending,
							  int64_t pending_ts,
							  const char *last_streak_date_override,
							  const char *today_date_override,
							  const char *pending_date,
							  const char *timezone,
							  int64_t now) {
	if (!c || !key || !lo || !hi) return -1;

	char count_buf[32];
	char lrt_buf[32];
	char pts_buf[32];
	snprintf(count_buf, sizeof count_buf, "%ld", count);
	snprintf(lrt_buf, sizeof lrt_buf, "%lld", (long long)last_round_ts);
	snprintf(pts_buf, sizeof pts_buf, "%lld", (long long)pending_ts);

	const char *pending_str = "";
	if (pending == STREAK_PENDING_A)
		pending_str = "a";
	else if (pending == STREAK_PENDING_B)
		pending_str = "b";

	char last_streak_date[11];
	char today_date[11];
	epoch_to_iso(last_round_ts, last_streak_date);
	epoch_to_iso(now, today_date);
	if (last_streak_date_override) snprintf(last_streak_date, sizeof last_streak_date, "%s", last_streak_date_override);
	if (today_date_override) snprintf(today_date, sizeof today_date, "%s", today_date_override);

	bool last_round_is_today =
		last_streak_date[0] && today_date[0] && strcmp(last_streak_date, today_date) == 0;
	bool a_today = (pending == STREAK_PENDING_A) || (pending == STREAK_PENDING_NONE && last_round_is_today);
	bool b_today = (pending == STREAK_PENDING_B) || (pending == STREAK_PENDING_NONE && last_round_is_today);

	return redisAppendCommand(c,
							  "HMSET %b "
							  "count %s "
							  "user_a_id %b "
							  "user_b_id %b "
							  "last_round_ts %s "
							  "pending %s "
							  "pending_ts %s "
							  "last_streak_date %s "
							  "today_date %s "
							  "pending_date %s "
							  "timezone %s "
							  "user_a_today %s "
							  "user_b_today %s",
							  key, key_len,
							  count_buf,
							  lo, lo_len,
							  hi, hi_len,
							  lrt_buf,
							  pending_str,
							  pts_buf,
							  last_streak_date,
							  today_date,
							  pending_date ? pending_date : "",
							  timezone ? timezone : "UTC",
							  a_today ? "1" : "0",
							  b_today ? "1" : "0") == REDIS_OK
			   ? 0
			   : -1;
}

int redis_streak_append_activity_hmset(redisContext *c, const char *key, size_t key_len,
									   const struct streak_state *s) {
	if (!c || !key || !s) return -1;
	char a_ts[32], b_ts[32];
	snprintf(a_ts, sizeof a_ts, "%lld", (long long)s->activity_a_ts);
	snprintf(b_ts, sizeof b_ts, "%lld", (long long)s->activity_b_ts);
	return redisAppendCommand(c,
							  "HMSET %b timezone_a %s timezone_b %s "
							  "activity_a_ts %s activity_b_ts %s "
							  "activity_a_date %s activity_b_date %s "
							  "today_a_date %s today_b_date %s "
							  "completed_a_date %s completed_b_date %s "
							  "user_a_today %s user_b_today %s",
							  key, key_len,
							  s->timezone_a[0] ? s->timezone_a : "",
							  s->timezone_b[0] ? s->timezone_b : "",
							  a_ts, b_ts,
							  s->activity_a_date, s->activity_b_date,
							  s->today_a_date, s->today_b_date,
							  s->completed_a_date, s->completed_b_date,
							  s->user_a_today ? "1" : "0", s->user_b_today ? "1" : "0") == REDIS_OK
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
	const char *last_round_ts = NULL;
	size_t lrt_len = 0;
	const char *pending = NULL;
	size_t pending_len = 0;
	const char *pending_ts = NULL;
	size_t pts_len = 0;
	const char *timezone = NULL;
	size_t timezone_len = 0;
	const char *timezone_a = NULL, *timezone_b = NULL;
	size_t timezone_a_len = 0, timezone_b_len = 0;
	const char *activity_a_date = NULL, *activity_b_date = NULL;
	size_t activity_a_len = 0, activity_b_len = 0;
	const char *today_a_date = NULL, *today_b_date = NULL;
	size_t today_a_len = 0, today_b_len = 0;
	const char *complete_a_date = NULL, *complete_b_date = NULL;
	size_t complete_a_len = 0, complete_b_len = 0;
	bool complete_today = false;

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
		} else if (k->len == 13 && memcmp(k->str, "last_round_ts", 13) == 0) {
			last_round_ts = v->str;
			lrt_len = (size_t)v->len;
		} else if (k->len == 7 && memcmp(k->str, "pending", 7) == 0) {
			pending = v->str;
			pending_len = (size_t)v->len;
		} else if (k->len == 10 && memcmp(k->str, "pending_ts", 10) == 0) {
			pending_ts = v->str;
			pts_len = (size_t)v->len;
		} else if (k->len == 8 && memcmp(k->str, "timezone", 8) == 0) {
			timezone = v->str;
			timezone_len = (size_t)v->len;
		} else if (k->len == 10 && memcmp(k->str, "timezone_a", 10) == 0) {
			timezone_a = v->str;
			timezone_a_len = (size_t)v->len;
		} else if (k->len == 10 && memcmp(k->str, "timezone_b", 10) == 0) {
			timezone_b = v->str;
			timezone_b_len = (size_t)v->len;
		} else if (k->len == 15 && memcmp(k->str, "activity_a_date", 15) == 0) {
			activity_a_date = v->str;
			activity_a_len = (size_t)v->len;
		} else if (k->len == 15 && memcmp(k->str, "activity_b_date", 15) == 0) {
			activity_b_date = v->str;
			activity_b_len = (size_t)v->len;
		} else if (k->len == 12 && memcmp(k->str, "today_a_date", 12) == 0) {
			today_a_date = v->str;
			today_a_len = (size_t)v->len;
		} else if (k->len == 12 && memcmp(k->str, "today_b_date", 12) == 0) {
			today_b_date = v->str;
			today_b_len = (size_t)v->len;
		} else if (k->len == 16 && memcmp(k->str, "completed_a_date", 16) == 0) {
			complete_a_date = v->str;
			complete_a_len = (size_t)v->len;
		} else if (k->len == 16 && memcmp(k->str, "completed_b_date", 16) == 0) {
			complete_b_date = v->str;
			complete_b_len = (size_t)v->len;
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

	long long last_round_ts_v = 0;
	if (last_round_ts && lrt_len < 32) {
		char tmp[32];
		memcpy(tmp, last_round_ts, lrt_len);
		tmp[lrt_len] = '\0';
		last_round_ts_v = strtoll(tmp, NULL, 10);
	}
	long long pending_ts_v = 0;
	if (pending_ts && pts_len < 32) {
		char tmp[32];
		memcpy(tmp, pending_ts, pts_len);
		tmp[pts_len] = '\0';
		pending_ts_v = strtoll(tmp, NULL, 10);
	}

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
	rc |= equi_buf_puts(out, ",\"last_round_ts\":");
	rc |= equi_buf_printf(out, "%lld", last_round_ts_v);
	rc |= equi_buf_puts(out, ",\"pending\":");
	if (pending && pending_len > 0) {
		rc |= equi_buf_putc(out, '"');
		rc |= equi_buf_putn(out, pending, pending_len);
		rc |= equi_buf_putc(out, '"');
	} else {
		rc |= equi_buf_puts(out, "\"\"");
	}
	rc |= equi_buf_puts(out, ",\"pending_ts\":");
	rc |= equi_buf_printf(out, "%lld", pending_ts_v);
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
	rc |= equi_buf_puts(out, ",\"timezone\":\"");
	if (timezone && timezone_len)
		rc |= equi_buf_putn(out, timezone, timezone_len);
	else
		rc |= equi_buf_puts(out, "UTC");
	rc |= equi_buf_putc(out, '\"');
	rc |= equi_buf_puts(out, ",\"timezone_a\":\"");
	if (timezone_a && timezone_a_len)
		rc |= equi_buf_putn(out, timezone_a, timezone_a_len);
	else if (timezone && timezone_len)
		rc |= equi_buf_putn(out, timezone, timezone_len);
	else
		rc |= equi_buf_puts(out, "UTC");
	rc |= equi_buf_puts(out, "\",\"timezone_b\":\"");
	if (timezone_b && timezone_b_len)
		rc |= equi_buf_putn(out, timezone_b, timezone_b_len);
	else if (timezone && timezone_len)
		rc |= equi_buf_putn(out, timezone, timezone_len);
	else
		rc |= equi_buf_puts(out, "UTC");
	rc |= equi_buf_puts(out, "\",\"timezone_a_known\":");
	rc |= equi_buf_puts(out, timezone_a && timezone_a_len ? "true" : "false");
	rc |= equi_buf_puts(out, ",\"timezone_b_known\":");
	rc |= equi_buf_puts(out, timezone_b && timezone_b_len ? "true" : "false");
	rc |= equi_buf_puts(out, ",\"user_a_activity_date\":");
	if (activity_a_date && activity_a_len) {
		rc |= equi_buf_putc(out, '\"');
		rc |= equi_buf_putn(out, activity_a_date, activity_a_len);
		rc |= equi_buf_putc(out, '\"');
	} else
		rc |= equi_buf_puts(out, "null");
	rc |= equi_buf_puts(out, ",\"user_b_activity_date\":");
	if (activity_b_date && activity_b_len) {
		rc |= equi_buf_putc(out, '\"');
		rc |= equi_buf_putn(out, activity_b_date, activity_b_len);
		rc |= equi_buf_putc(out, '\"');
	} else
		rc |= equi_buf_puts(out, "null");
	rc |= equi_buf_puts(out, ",\"user_a_today_date\":");
	if (today_a_date && today_a_len) {
		rc |= equi_buf_putc(out, '\"');
		rc |= equi_buf_putn(out, today_a_date, today_a_len);
		rc |= equi_buf_putc(out, '\"');
	} else
		rc |= equi_buf_puts(out, "null");
	rc |= equi_buf_puts(out, ",\"user_b_today_date\":");
	if (today_b_date && today_b_len) {
		rc |= equi_buf_putc(out, '\"');
		rc |= equi_buf_putn(out, today_b_date, today_b_len);
		rc |= equi_buf_putc(out, '\"');
	} else
		rc |= equi_buf_puts(out, "null");
	complete_today = a_today && b_today;
	rc |= equi_buf_puts(out, ",\"complete_today\":");
	rc |= equi_buf_puts(out, complete_today ? "true" : "false");
	rc |= equi_buf_puts(out, ",\"completed_a_date\":");
	if (complete_a_date && complete_a_len) {
		rc |= equi_buf_putc(out, '\"');
		rc |= equi_buf_putn(out, complete_a_date, complete_a_len);
		rc |= equi_buf_putc(out, '\"');
	} else
		rc |= equi_buf_puts(out, "null");
	rc |= equi_buf_puts(out, ",\"completed_b_date\":");
	if (complete_b_date && complete_b_len) {
		rc |= equi_buf_putc(out, '\"');
		rc |= equi_buf_putn(out, complete_b_date, complete_b_len);
		rc |= equi_buf_putc(out, '\"');
	} else
		rc |= equi_buf_puts(out, "null");
	rc |= equi_buf_putc(out, '}');
	return rc;
}
