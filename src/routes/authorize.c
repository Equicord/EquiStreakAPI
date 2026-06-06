// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#include "authorize.h"

#include "../auth/oauth.h"
#include "../auth/ratelimit.h"
#include "../config.h"
#include "../http/request.h"
#include "../http/response.h"
#include "../http/router.h"
#include "../log.h"

#include <stdio.h>
#include <stdlib.h>

static int handle_authorize(struct http_request *r, void *userdata) {
	struct config *cfg = userdata;
	if (!ratelimit_check_ip_ok(http_req_client_ip(r))) {
		return http_send_too_many(r->conn, 60);
	}
	const char *err = http_req_query(r, "error");
	if (err && err[0]) {
		char msg[256];
		const char *desc = http_req_query(r, "error_description");
		snprintf(msg, sizeof msg, "Discord returned error: %s%s%s",
				 err,
				 desc && desc[0] ? " - " : "",
				 desc && desc[0] ? desc : "");
		return http_send_error(r->conn, 400, msg);
	}
	const char *code = http_req_query(r, "code");
	if (!code || !code[0]) {
		return http_send_error(r->conn, 400, "Missing code parameter");
	}

	char *body = NULL;
	size_t len = 0;
	long http_code = 0;
	int rc = oauth_exchange_code(cfg, code, &body, &len, &http_code);
	if (rc != 0 || !body) {
		free(body);
		log_warn("oauth_exchange_code failed (curl=%d http=%ld)", rc, http_code);
		return http_send_error(r->conn, 502, "OAuth2 exchange failed");
	}

	if (http_code < 200 || http_code >= 300) {
		int sent = http_send_text(r->conn, (unsigned)http_code, "application/json", body, len);
		free(body);
		return sent;
	}

	int sent = http_send_text(r->conn, 200, "application/json", body, len);
	free(body);
	return sent;
}

void routes_authorize_register(struct http_router *r, struct config *cfg) {
	router_add(r, "GET", "/api/authorize", handle_authorize, cfg);
}
