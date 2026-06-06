// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef EQUISTREAKAPI_AUTH_OAUTH_H
#define EQUISTREAKAPI_AUTH_OAUTH_H

#include <stddef.h>

struct config;

int oauth_exchange_code(const struct config *cfg, const char *code,
						char **out_response, size_t *out_len, long *out_http_code);

#endif
