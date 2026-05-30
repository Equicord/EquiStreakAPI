// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#include "config.h"

#include "log.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char *dup_env(const char *name) {
	const char *v = getenv(name);
	if (!v || !v[0]) return NULL;
	return strdup(v);
}

static int parse_int_env(const char *name, int defv, int minv, int maxv) {
	const char *v = getenv(name);
	if (!v || !v[0]) return defv;
	char *end = NULL;
	long n = strtol(v, &end, 10);
	if (end == v || (end && *end)) {
		log_warn("invalid %s=%s, using default %d", name, v, defv);
		return defv;
	}
	if (n < minv || n > maxv) {
		log_warn("%s=%ld out of range [%d,%d], using default %d", name, n, minv, maxv, defv);
		return defv;
	}
	return (int)n;
}

void config_init(struct config *cfg) {
	memset(cfg, 0, sizeof *cfg);
	cfg->port = 3000;
	cfg->bind = strdup("0.0.0.0");
	cfg->public_dir = NULL;
	cfg->rate_limit_per_min = 60;
	cfg->auth_cache_ttl_s = 300;
	long n = sysconf(_SC_NPROCESSORS_ONLN);
	cfg->thread_pool_size = (n > 0) ? (int)n : 4;
	if (cfg->thread_pool_size > 32) cfg->thread_pool_size = 32;
}

void config_load_env(struct config *cfg) {
	int p = parse_int_env("PORT", cfg->port, 1, 65535);
	cfg->port = (uint16_t)p;

	char *v;
	if ((v = dup_env("BIND")) != NULL) {
		free(cfg->bind);
		cfg->bind = v;
	}
	if ((v = dup_env("REDIS_URL")) != NULL) cfg->redis_url = v;
	if ((v = dup_env("DISCORD_CLIENT_ID")) != NULL) cfg->discord_client_id = v;
	if ((v = dup_env("DISCORD_CLIENT_SECRET")) != NULL) cfg->discord_client_secret = v;
	if ((v = dup_env("DISCORD_REDIRECT_URI")) != NULL) cfg->discord_redirect_uri = v;
	if ((v = dup_env("MASTER_API_KEY")) != NULL) cfg->master_api_key = v;

	if (!cfg->redis_url) cfg->redis_url = strdup("redis://localhost:6379");
	if (!cfg->discord_redirect_uri) {
		char *u = NULL;
		if (equi_xasprintf(&u, "http://localhost:%u/api/authorize", (unsigned)cfg->port) == 0 && u) {
			cfg->discord_redirect_uri = u;
		}
	}

	cfg->rate_limit_per_min = parse_int_env("RATE_LIMIT_PER_MIN", cfg->rate_limit_per_min, 1, 100000);
	cfg->auth_cache_ttl_s = parse_int_env("AUTH_CACHE_TTL_S", cfg->auth_cache_ttl_s, 0, 86400);
	cfg->thread_pool_size = parse_int_env("THREAD_POOL_SIZE", cfg->thread_pool_size, 1, 256);

	const char *xff = getenv("TRUST_XFF");
	cfg->trust_xff = (xff && (xff[0] == '1' || xff[0] == 't' || xff[0] == 'T'));
}

void config_validate_or_die(const struct config *cfg) {
	if (!cfg->discord_client_id || !cfg->discord_client_id[0])
		die("DISCORD_CLIENT_ID is required");
	if (!cfg->discord_client_secret || !cfg->discord_client_secret[0])
		die("DISCORD_CLIENT_SECRET is required");
	if (!cfg->master_api_key || !cfg->master_api_key[0])
		die("MASTER_API_KEY is required");
	if (strlen(cfg->master_api_key) < 32)
		die("MASTER_API_KEY must be at least 32 characters");
	if (!cfg->public_dir || !cfg->public_dir[0])
		die("public_dir is required (use --public-dir or install to PREFIX)");
}

void config_free(struct config *cfg) {
	free(cfg->bind);
	free(cfg->public_dir);
	free(cfg->redis_url);
	free(cfg->discord_client_id);
	free(cfg->discord_client_secret);
	free(cfg->discord_redirect_uri);
	free(cfg->master_api_key);
	memset(cfg, 0, sizeof *cfg);
}
