// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#include "cache.h"

#include "../config.h"
#include "../counters.h"
#include "../redis/redis.h"
#include "../util.h"
#include "../vendor/sha256/sha256.h"
#include "discord.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_ttl = 300;
static uint8_t g_hmac_key[SHA256_DIGEST_SIZE];

void auth_cache_init(const struct config *cfg) {
	g_ttl = cfg ? cfg->auth_cache_ttl_s : 300;

	const char *secret = (cfg && cfg->master_api_key) ? cfg->master_api_key : "";
	struct sha256_ctx ctx;
	sha256_init(&ctx);
	sha256_update(&ctx, (const uint8_t *)"equi-auth-cache-v1:", 19);
	sha256_update(&ctx, (const uint8_t *)secret, strlen(secret));
	sha256_final(&ctx, g_hmac_key);
}

static void token_key(const char *bearer_token, char out[80]) {
	uint8_t digest[SHA256_DIGEST_SIZE];
	hmac_sha256(g_hmac_key, sizeof g_hmac_key,
				(const uint8_t *)bearer_token, strlen(bearer_token),
				digest);
	char hex[SHA256_HEX_SIZE];
	sha256_to_hex(digest, hex);
	snprintf(out, 80, "authcache:%s", hex);
}

static int auth_cache_lookup(const char *bearer_token, char *out_id, size_t cap) {
	if (!bearer_token || !out_id || cap == 0) return -1;
	if (g_ttl <= 0) return -1;
	redisContext *c = redis_ctx();
	if (!c) return -1;

	char key[80];
	token_key(bearer_token, key);

	redisReply *r = redisCommand(c, "GET %s", key);
	if (!r) {
		redis_invalidate_ctx();
		return -1;
	}
	int rc = -1;
	if (r->type == REDIS_REPLY_STRING && (size_t)r->len < cap) {
		memcpy(out_id, r->str, (size_t)r->len);
		out_id[r->len] = '\0';
		if (equi_is_discord_id(out_id))
			rc = 0;
		else
			out_id[0] = '\0';
	}
	freeReplyObject(r);
	return rc;
}

static void auth_cache_store(const char *bearer_token, const char *user_id) {
	if (!bearer_token || !user_id || g_ttl <= 0) return;
	redisContext *c = redis_ctx();
	if (!c) return;

	char key[80];
	token_key(bearer_token, key);

	redisReply *r = redisCommand(c, "SET %s %s EX %d", key, user_id, g_ttl);
	if (r) freeReplyObject(r);
}

int auth_resolve_user(const char *bearer_token, char *out_id, size_t cap) {
	if (auth_cache_lookup(bearer_token, out_id, cap) == 0) {
		counter_cache_inc(CACHE_HIT);
		return 0;
	}
	counter_cache_inc(CACHE_MISS);
	if (discord_get_user_id(bearer_token, out_id, cap) != 0) return -1;
	auth_cache_store(bearer_token, out_id);
	return 0;
}
