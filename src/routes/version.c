// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#include "version.h"

#include "../http/request.h"
#include "../http/response.h"
#include "../http/router.h"

#include <stdio.h>
#include <time.h>

#ifndef EQUISTREAKAPI_VERSION
#define EQUISTREAKAPI_VERSION "0.1.0"
#endif
#ifndef EQUISTREAKAPI_GIT_SHA
#define EQUISTREAKAPI_GIT_SHA "unknown"
#endif
#ifndef EQUISTREAKAPI_BUILD_TIME
#define EQUISTREAKAPI_BUILD_TIME "unknown"
#endif

static time_t g_start_epoch = 0;

static int handle_version(struct http_request *r, void *userdata) {
	(void)userdata;
	char body[320];
	int n = snprintf(body, sizeof body,
					 "{\"version\":\"%s\",\"git_sha\":\"%s\",\"build_time\":\"%s\","
					 "\"schema_version\":%d,\"uptime_seconds\":%lld}",
					 EQUISTREAKAPI_VERSION, EQUISTREAKAPI_GIT_SHA, EQUISTREAKAPI_BUILD_TIME,
					 EQUISTREAKAPI_SCHEMA_VERSION,
					 (long long)(time(NULL) - g_start_epoch));
	if (n < 0 || n >= (int)sizeof body) return http_send_error(r->conn, 500, "Internal server error");
	return http_send_text(r->conn, 200, "application/json", body, (size_t)n);
}

void routes_version_register(struct http_router *r, struct config *cfg) {
	(void)cfg;
	g_start_epoch = time(NULL);
	router_add(r, "GET", "/version", handle_version, NULL);
}
