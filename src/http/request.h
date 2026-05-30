// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef EQUISTREAKAPI_HTTP_REQUEST_H
#define EQUISTREAKAPI_HTTP_REQUEST_H

#include <stdbool.h>
#include <stddef.h>

struct MHD_Connection;
struct json_object;

struct http_request {
	struct MHD_Connection *conn;
	const char *method;
	const char *path;
	const char *query_string;

	char *route_params[8];
	const char *route_param_names[8];
	int route_param_count;

	char *body;
	size_t body_len;
	size_t body_cap;

	bool rejected;

	char req_id[17];
	unsigned long long start_ms;
	char client_ip[64];
};

const char *http_req_header(struct http_request *r, const char *name);
const char *http_req_query(struct http_request *r, const char *name);
const char *http_req_param(struct http_request *r, const char *name);

struct json_object *http_req_json(struct http_request *r);

const char *http_req_client_ip(struct http_request *r);

void http_set_trust_xff(bool trust);

#endif
