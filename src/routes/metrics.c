// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#include "metrics.h"

#include "../http/request.h"
#include "../http/response.h"
#include "../http/router.h"
#include "../log.h"
#include "../redis/pipeline.h"
#include "../redis/redis.h"
#include "../uid_set.h"

#include <microhttpd.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static time_t g_start_epoch = 0;

static time_t iso_to_utc_midnight(int y, int m, int d) {
	if (m < 1 || m > 12 || d < 1 || d > 31) return (time_t)-1;
	y -= m <= 2;
	int era = (y >= 0 ? y : y - 399) / 400;
	unsigned yoe = (unsigned)(y - era * 400);
	unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
	unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	long days = (long)era * 146097 + (long)doe - 719468;
	return (time_t)days * 86400;
}

static long days_between_iso(const char *date_iso, time_t now_midnight_utc) {
	if (!date_iso || strlen(date_iso) < 10) return -1;
	int y = 0, mo = 0, d = 0;
	if (sscanf(date_iso, "%4d-%2d-%2d", &y, &mo, &d) != 3) return -1;
	time_t t = iso_to_utc_midnight(y, mo, d);
	if (t == (time_t)-1) return -1;
	long diff = (long)((now_midnight_utc - t) / 86400);
	return diff;
}

static void fold_streak_reply(redisReply *resp, time_t midnight,
							  long *day, long *week, long *month,
							  struct uid_set *uday, struct uid_set *uweek, struct uid_set *umonth) {
	if (!resp || resp->type != REDIS_REPLY_ARRAY || resp->elements != 4) return;

	redisReply *e_last = resp->element[0];
	redisReply *e_a = resp->element[1];
	redisReply *e_b = resp->element[2];
	redisReply *e_today = resp->element[3];

	char buf[16];
	const char *active = NULL;
	if (e_today->type == REDIS_REPLY_STRING && e_today->len > 0 && (size_t)e_today->len < sizeof buf) {
		memcpy(buf, e_today->str, (size_t)e_today->len);
		buf[e_today->len] = '\0';
		active = buf;
	} else if (e_last->type == REDIS_REPLY_STRING && e_last->len > 0 && (size_t)e_last->len < sizeof buf) {
		memcpy(buf, e_last->str, (size_t)e_last->len);
		buf[e_last->len] = '\0';
		active = buf;
	}
	if (!active) return;

	long days = days_between_iso(active, midnight);
	if (days < 0) return;

	const char *uid_a = (e_a->type == REDIS_REPLY_STRING) ? e_a->str : NULL;
	size_t uid_a_len = (e_a->type == REDIS_REPLY_STRING) ? (size_t)e_a->len : 0;
	const char *uid_b = (e_b->type == REDIS_REPLY_STRING) ? e_b->str : NULL;
	size_t uid_b_len = (e_b->type == REDIS_REPLY_STRING) ? (size_t)e_b->len : 0;

	if (days <= 1) {
		(*day)++;
		uid_set_add(uday, uid_a, uid_a_len);
		uid_set_add(uday, uid_b, uid_b_len);
	}
	if (days <= 7) {
		(*week)++;
		uid_set_add(uweek, uid_a, uid_a_len);
		uid_set_add(uweek, uid_b, uid_b_len);
	}
	if (days <= 30) {
		(*month)++;
		uid_set_add(umonth, uid_a, uid_a_len);
		uid_set_add(umonth, uid_b, uid_b_len);
	}
}

#define SCAN_MAX_ITERS 100000

static bool parse_cursor(const char *s, unsigned long long *out) {
	if (!s || !*s) return false;
	char *end = NULL;
	unsigned long long v = strtoull(s, &end, 10);
	if (!end || *end) return false;
	*out = v;
	return true;
}

static long scan_count(redisContext *c, const char *pattern) {
	long total = 0;
	unsigned long long cursor = 0;
	int iters = 0;
	do {
		if (++iters > SCAN_MAX_ITERS) {
			log_warn("scan_count: iteration cap exceeded (cap=%d pattern=%s); keyspace may be too large",
					 SCAN_MAX_ITERS, pattern);
			return -1;
		}
		redisReply *r = redisCommand(c, "SCAN %llu MATCH %s COUNT 500", cursor, pattern);
		if (!r || r->type != REDIS_REPLY_ARRAY || r->elements != 2) {
			redis_reply_safe_free(r);
			return -1;
		}
		redisReply *cur = r->element[0];
		redisReply *arr = r->element[1];
		if (cur->type != REDIS_REPLY_STRING || arr->type != REDIS_REPLY_ARRAY ||
			!parse_cursor(cur->str, &cursor)) {
			redis_reply_safe_free(r);
			return -1;
		}
		total += (long)arr->elements;
		redis_reply_safe_free(r);
	} while (cursor != 0);
	return total;
}

static int handle_metrics(struct http_request *r, void *userdata) {
	(void)userdata;
	redisContext *c = redis_ctx_or_503(r->conn);
	if (!c) return MHD_YES;

	time_t now = time(NULL);
	struct tm tm;
	gmtime_r(&now, &tm);
	time_t midnight = iso_to_utc_midnight(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);

	long users_total = scan_count(c, "user_streaks:*");
	if (users_total < 0) {
		redis_invalidate_ctx();
		return http_send_error(r->conn, 503, "Redis error");
	}

	long streaks_day = 0, streaks_week = 0, streaks_month = 0;
	long streaks_total = 0;
	struct uid_set users_day = {0}, users_week = {0}, users_month = {0};

	unsigned long long cursor = 0;
	int iters = 0;
	bool ctx_dead = false;
	do {
		if (++iters > SCAN_MAX_ITERS) {
			log_warn("metrics: scan iteration cap exceeded (cap=%d streaks_so_far=%ld)",
					 SCAN_MAX_ITERS, streaks_total);
			ctx_dead = true;
			break;
		}
		redisReply *r_scan = redisCommand(c, "SCAN %llu MATCH streak:* COUNT 500", cursor);
		if (!r_scan || r_scan->type != REDIS_REPLY_ARRAY || r_scan->elements != 2) {
			redis_reply_safe_free(r_scan);
			ctx_dead = true;
			break;
		}
		redisReply *cur = r_scan->element[0];
		redisReply *arr = r_scan->element[1];
		if (cur->type != REDIS_REPLY_STRING || arr->type != REDIS_REPLY_ARRAY ||
			!parse_cursor(cur->str, &cursor)) {
			redis_reply_safe_free(r_scan);
			ctx_dead = true;
			break;
		}
		streaks_total += (long)arr->elements;
		size_t appended = 0;
		for (size_t i = 0; i < arr->elements; i++) {
			redisReply *ke = arr->element[i];
			if (ke->type != REDIS_REPLY_STRING) continue;
			if (redisAppendCommand(c, "HMGET %b last_streak_date user_a_id user_b_id today_date",
								   ke->str, (size_t)ke->len) != REDIS_OK) {
				ctx_dead = true;
				break;
			}
			appended++;
		}
		for (size_t i = 0; i < appended; i++) {
			redisReply *resp = NULL;
			if (redisGetReply(c, (void **)&resp) != REDIS_OK) {
				redis_reply_safe_free(resp);
				ctx_dead = true;
				break;
			}
			fold_streak_reply(resp, midnight,
							  &streaks_day, &streaks_week, &streaks_month,
							  &users_day, &users_week, &users_month);
			redis_reply_safe_free(resp);
		}
		redis_reply_safe_free(r_scan);
		if (ctx_dead) break;
	} while (cursor != 0);

	if (ctx_dead) {
		uid_set_free(&users_day);
		uid_set_free(&users_week);
		uid_set_free(&users_month);
		redis_invalidate_ctx();
		return http_send_error(r->conn, 503, "Redis error");
	}

	char body[512];
	int n = snprintf(body, sizeof body,
					 "{\"timestamp\":%lld,\"uptime_seconds\":%lld,"
					 "\"users_day\":%zu,\"users_week\":%zu,\"users_month\":%zu,\"users_total\":%ld,"
					 "\"streaks_day\":%ld,\"streaks_week\":%ld,\"streaks_month\":%ld,\"streaks_total\":%ld}",
					 (long long)now, (long long)(now - g_start_epoch),
					 users_day.count, users_week.count, users_month.count, users_total,
					 streaks_day, streaks_week, streaks_month, streaks_total);

	uid_set_free(&users_day);
	uid_set_free(&users_week);
	uid_set_free(&users_month);

	if (n < 0 || n >= (int)sizeof body) return http_send_error(r->conn, 500, "Internal server error");
	return http_send_text(r->conn, 200, "application/json", body, (size_t)n);
}

void routes_metrics_register(struct http_router *r, struct config *cfg) {
	(void)cfg;
	g_start_epoch = time(NULL);
	router_add(r, "GET", "/metrics", handle_metrics, NULL);
}
