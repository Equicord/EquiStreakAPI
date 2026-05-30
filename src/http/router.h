// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef EQUISTREAKAPI_HTTP_ROUTER_H
#define EQUISTREAKAPI_HTTP_ROUTER_H

#include "request.h"

struct http_router;
struct MHD_Connection;

typedef int (*http_handler_fn)(struct http_request *r, void *userdata);

struct http_router *router_create(void);
void router_destroy(struct http_router *r);

int router_add(struct http_router *r, const char *method, const char *path_pattern,
			   http_handler_fn handler, void *userdata);

void router_set_fallback(struct http_router *r, http_handler_fn handler, void *userdata);

int router_dispatch(struct http_router *r, struct http_request *req);

#endif
