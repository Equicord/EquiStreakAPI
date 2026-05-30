// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef EQUISTREAKAPI_AUTH_RATELIMIT_H
#define EQUISTREAKAPI_AUTH_RATELIMIT_H

#include <stdbool.h>

struct config;

void ratelimit_init(const struct config *cfg);

bool ratelimit_check_ok(const char *user_id);
bool ratelimit_check_ip_ok(const char *client_ip);

#endif
