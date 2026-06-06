// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#include "health.h"

#include "../http/request.h"
#include "../http/response.h"
#include "../http/router.h"
#include "../redis/redis.h"
#include "../shutdown.h"

static const char OK_FULL[] = "{\"status\":\"ok\",\"redis\":\"connected\"}";
static const char ERR_FULL[] = "{\"status\":\"error\",\"redis\":\"disconnected\"}";
static const char DRAIN_FULL[] = "{\"status\":\"draining\",\"redis\":\"unknown\"}";
static const char OK_LIVE[] = "{\"status\":\"ok\"}";
static const char ERR_DRAIN_LIVE[] = "{\"error\":\"draining\"}";

static int handle_health(struct http_request *r, void *userdata) {
	(void)userdata;
	if (is_draining())
		return http_send_static(r->conn, 503, "application/json", DRAIN_FULL, sizeof DRAIN_FULL - 1);
	if (redis_ping_ok())
		return http_send_static(r->conn, 200, "application/json", OK_FULL, sizeof OK_FULL - 1);
	return http_send_static(r->conn, 503, "application/json", ERR_FULL, sizeof ERR_FULL - 1);
}

static int handle_live(struct http_request *r, void *userdata) {
	(void)userdata;
	if (is_draining())
		return http_send_static(r->conn, 503, "application/json", ERR_DRAIN_LIVE, sizeof ERR_DRAIN_LIVE - 1);
	return http_send_static(r->conn, 200, "application/json", OK_LIVE, sizeof OK_LIVE - 1);
}

void routes_health_register(struct http_router *r, struct config *cfg) {
	(void)cfg;
	router_add(r, "GET", "/health", handle_health, NULL);
	router_add(r, "GET", "/health/live", handle_live, NULL);
	router_add(r, "GET", "/health/ready", handle_health, NULL);
}
