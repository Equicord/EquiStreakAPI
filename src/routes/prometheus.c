// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#include "prometheus.h"

#include "../counters.h"
#include "../http/request.h"
#include "../http/response.h"
#include "../http/router.h"

#include <stdlib.h>

static int handle_prometheus(struct http_request *r, void *userdata) {
	(void)userdata;
	char *body = NULL;
	size_t len = 0;
	if (counters_render_prometheus(&body, &len) != 0 || !body) {
		return http_send_error(r->conn, 500, "Internal server error");
	}
	int rc = http_send_text(r->conn, 200, "text/plain; version=0.0.4", body, len);
	free(body);
	return rc;
}

void routes_prometheus_register(struct http_router *r, struct config *cfg) {
	(void)cfg;
	router_add(r, "GET", "/metrics/prometheus", handle_prometheus, NULL);
}
