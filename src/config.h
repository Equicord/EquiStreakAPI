// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef EQUISTREAKAPI_CONFIG_H
#define EQUISTREAKAPI_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

struct config {
	uint16_t port;
	char *bind;
	char *public_dir;

	char *redis_url;
	char *discord_client_id;
	char *discord_client_secret;
	char *discord_redirect_uri;
	char *master_api_key;

	int rate_limit_per_min;
	int auth_cache_ttl_s;

	int thread_pool_size;

	bool trust_xff;
};

void config_init(struct config *cfg);
void config_load_env(struct config *cfg);
void config_validate_or_die(const struct config *cfg);
void config_free(struct config *cfg);

#endif
