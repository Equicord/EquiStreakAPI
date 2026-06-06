// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef EQUISTREAKAPI_HTTP_SERVER_H
#define EQUISTREAKAPI_HTTP_SERVER_H

struct config;
struct http_router;
struct http_server;

struct http_server *http_server_start(const struct config *cfg, struct http_router *router);
void http_server_stop(struct http_server *s);

#endif
