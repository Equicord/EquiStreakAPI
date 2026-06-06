// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef EQUISTREAKAPI_ROUTES_VERSION_H
#define EQUISTREAKAPI_ROUTES_VERSION_H

struct http_router;
struct config;

#define EQUISTREAKAPI_SCHEMA_VERSION 1

void routes_version_register(struct http_router *r, struct config *cfg);

#endif
