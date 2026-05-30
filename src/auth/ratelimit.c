// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#include "ratelimit.h"

#include "../config.h"
#include "../counters.h"
#include "../redis/redis.h"

#include <stdio.h>

static int g_limit = 60;

void ratelimit_init(const struct config *cfg) {
	g_limit = cfg ? cfg->rate_limit_per_min : 60;
	if (g_limit < 1) g_limit = 1;
}

static bool check_bucket(const char *key, long long limit) {
	redisContext *c = redis_ctx();
	if (!c) return true;
	redisReply *r = redisCommand(c, "INCR %s", key);
	if (!r) {
		redis_invalidate_ctx();
		return true;
	}
	long long current = (r->type == REDIS_REPLY_INTEGER) ? r->integer : 0;
	freeReplyObject(r);
	counter_redis_inc(REDIS_OP_OK);
	if (current == 1) {
		r = redisCommand(c, "EXPIRE %s 60", key);
		if (r) freeReplyObject(r);
	}
	return current <= limit;
}

bool ratelimit_check_ok(const char *user_id) {
	if (!user_id || !user_id[0]) return true;
	char key[128];
	int n = snprintf(key, sizeof key, "ratelimit:user:%s", user_id);
	if (n < 0 || n >= (int)sizeof key) return true;
	return check_bucket(key, g_limit);
}

bool ratelimit_check_ip_ok(const char *client_ip) {
	if (!client_ip || !client_ip[0]) return true;
	char key[128];
	int n = snprintf(key, sizeof key, "ratelimit:ip:%s", client_ip);
	if (n < 0 || n >= (int)sizeof key) return true;

	return check_bucket(key, (long long)g_limit * 6);
}
