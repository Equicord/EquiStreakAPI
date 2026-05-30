// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef EQUISTREAKAPI_HTTP_RESPONSE_H
#define EQUISTREAKAPI_HTTP_RESPONSE_H

#include <stdbool.h>
#include <stddef.h>

struct MHD_Connection;
struct MHD_Response;
struct json_object;

int http_send_json(struct MHD_Connection *conn, unsigned status, struct json_object *obj);
int http_send_text(struct MHD_Connection *conn, unsigned status, const char *mime, const char *body, size_t len);
int http_send_error(struct MHD_Connection *conn, unsigned status, const char *msg);
int http_send_static(struct MHD_Connection *conn, unsigned status, const char *mime, const void *body, size_t len);
int http_send_empty(struct MHD_Connection *conn, unsigned status);

int http_send_owned(struct MHD_Connection *conn, unsigned status, const char *mime, void *body, size_t len);

int http_send_too_many(struct MHD_Connection *conn, int retry_after_seconds);

void http_set_current_req_id(const char *id);

struct MHD_Response;
void http_apply_default_headers(struct MHD_Response *resp);

#endif
