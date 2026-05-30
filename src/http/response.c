// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#include "response.h"

#include "../util.h"

#include <json-c/json.h>
#include <microhttpd.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static __thread const char *tl_req_id;

void http_set_current_req_id(const char *id) {
	tl_req_id = id;
}

void http_apply_default_headers(struct MHD_Response *resp) {
	MHD_add_response_header(resp, "Access-Control-Allow-Origin", "*");
	MHD_add_response_header(resp, "Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
	MHD_add_response_header(resp, "Access-Control-Allow-Headers",
							"Origin, X-Requested-With, Content-Type, Accept, Authorization, x-api-key");
	if (tl_req_id && tl_req_id[0]) {
		MHD_add_response_header(resp, "X-Request-Id", tl_req_id);
	}
}

static int send_response(struct MHD_Connection *conn, unsigned status, const char *mime,
						 void *body, size_t len, enum MHD_ResponseMemoryMode mode) {
	struct MHD_Response *resp = MHD_create_response_from_buffer(len, body, mode);
	if (!resp) {
		if (mode == MHD_RESPMEM_MUST_FREE) free(body);
		return MHD_NO;
	}
	if (mime) MHD_add_response_header(resp, "Content-Type", mime);
	http_apply_default_headers(resp);
	int rc = MHD_queue_response(conn, status, resp);
	MHD_destroy_response(resp);
	return rc;
}

static int send_copy(struct MHD_Connection *conn, unsigned status, const char *mime,
					 const void *body, size_t len) {
	char *copy = malloc(len ? len : 1);
	if (!copy) return MHD_NO;
	if (len) memcpy(copy, body, len);
	return send_response(conn, status, mime, copy, len, MHD_RESPMEM_MUST_FREE);
}

int http_send_json(struct MHD_Connection *conn, unsigned status, struct json_object *obj) {
	const char *s = obj ? json_object_to_json_string_ext(obj, JSON_C_TO_STRING_PLAIN) : "null";
	int rc = send_copy(conn, status, "application/json", s, strlen(s));
	if (obj) json_object_put(obj);
	return rc;
}

int http_send_text(struct MHD_Connection *conn, unsigned status, const char *mime, const char *body, size_t len) {
	return send_copy(conn, status, mime, body, len);
}

int http_send_error(struct MHD_Connection *conn, unsigned status, const char *msg) {
	if (!msg) msg = "error";
	char esc[480];
	equi_json_escape(msg, esc, sizeof esc);
	char body[512];
	int n = snprintf(body, sizeof body, "{\"error\":\"%s\"}", esc);
	if (n < 0 || n >= (int)sizeof body) return MHD_NO;
	return send_copy(conn, status, "application/json", body, (size_t)n);
}

int http_send_static(struct MHD_Connection *conn, unsigned status, const char *mime, const void *body, size_t len) {
	return send_response(conn, status, mime, (void *)body, len, MHD_RESPMEM_PERSISTENT);
}

int http_send_empty(struct MHD_Connection *conn, unsigned status) {
	return send_response(conn, status, NULL, NULL, 0, MHD_RESPMEM_PERSISTENT);
}

int http_send_owned(struct MHD_Connection *conn, unsigned status, const char *mime, void *body, size_t len) {
	return send_response(conn, status, mime, body, len, MHD_RESPMEM_MUST_FREE);
}

int http_send_too_many(struct MHD_Connection *conn, int retry_after_seconds) {
	static const char body[] = "{\"error\":\"Too many requests\"}";
	char retry[16];
	snprintf(retry, sizeof retry, "%d", retry_after_seconds > 0 ? retry_after_seconds : 60);
	struct MHD_Response *resp = MHD_create_response_from_buffer(sizeof body - 1, (void *)body, MHD_RESPMEM_PERSISTENT);
	if (!resp) return MHD_NO;
	MHD_add_response_header(resp, "Content-Type", "application/json");
	MHD_add_response_header(resp, "Retry-After", retry);
	http_apply_default_headers(resp);
	int rc = MHD_queue_response(conn, 429, resp);
	MHD_destroy_response(resp);
	return rc;
}
