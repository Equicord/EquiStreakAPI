// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef EQUISTREAKAPI_AUTH_ADMIN_H
#define EQUISTREAKAPI_AUTH_ADMIN_H

#include <stdbool.h>

struct http_request;
struct config;

bool admin_check(struct http_request *r, const struct config *cfg);

#endif
