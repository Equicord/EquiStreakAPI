// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef EQUISTREAKAPI_AUTH_CACHE_H
#define EQUISTREAKAPI_AUTH_CACHE_H

#include <stddef.h>

struct config;

void auth_cache_init(const struct config *cfg);

int auth_resolve_user(const char *bearer_token, char *out_id, size_t cap);

#endif
