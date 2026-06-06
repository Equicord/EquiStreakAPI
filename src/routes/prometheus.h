// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef EQUISTREAKAPI_ROUTES_PROMETHEUS_H
#define EQUISTREAKAPI_ROUTES_PROMETHEUS_H

struct http_router;
struct config;

void routes_prometheus_register(struct http_router *r, struct config *cfg);

#endif
