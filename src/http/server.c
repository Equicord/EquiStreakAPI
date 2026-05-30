// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#include "server.h"

#include "../config.h"
#include "../counters.h"
#include "../log.h"
#include "../util.h"
#include "request.h"
#include "response.h"
#include "router.h"

#include <microhttpd.h>

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#define MAX_BODY_BYTES (1u << 20)

struct http_server {
	struct MHD_Daemon *daemon;
	struct http_router *router;
};

static int append_body(struct http_request *req, const char *data, size_t len) {
	if (req->body_len + len > MAX_BODY_BYTES) return -1;
	if (req->body_len + len + 1 > req->body_cap) {
		size_t cap = req->body_cap ? req->body_cap : 1024;
		while (cap < req->body_len + len + 1)
			cap *= 2;
		char *p = realloc(req->body, cap);
		if (!p) return -1;
		req->body = p;
		req->body_cap = cap;
	}
	memcpy(req->body + req->body_len, data, len);
	req->body_len += len;
	req->body[req->body_len] = '\0';
	return 0;
}

static void free_request(struct http_request *req) {
	if (!req) return;
	free(req->body);
	for (int i = 0; i < req->route_param_count; i++) {
		free(req->route_params[i]);
	}
	free(req);
}

static void request_completed_cb(void *cls, struct MHD_Connection *conn,
								 void **con_cls, enum MHD_RequestTerminationCode toe) {
	(void)cls;
	(void)toe;
	if (!con_cls || !*con_cls) return;
	struct http_request *req = *con_cls;

	const union MHD_ConnectionInfo *ci =
		MHD_get_connection_info(conn, MHD_CONNECTION_INFO_HTTP_STATUS);
	unsigned status = (ci && ci->http_status) ? (unsigned)ci->http_status : 0;
	unsigned long long dur = (req->start_ms != 0) ? equi_now_ms() - req->start_ms : 0;

	counter_http_inc(method_kind_of(req->method), status_class_of(status));
	if (log_access_enabled()) {
		log_access(req->method, req->path, status, dur, req->req_id,
				   http_req_client_ip(req));
	}

	free_request(req);
	*con_cls = NULL;
}

static enum MHD_Result access_cb(void *cls, struct MHD_Connection *conn,
								 const char *url, const char *method,
								 const char *version, const char *upload_data,
								 size_t *upload_data_size, void **con_cls) {
	(void)version;
	struct http_server *s = cls;

	if (*con_cls == NULL) {
		struct http_request *req = calloc(1, sizeof *req);
		if (!req) return MHD_NO;
		req->conn = conn;
		req->method = method;
		req->path = url;
		req->start_ms = equi_now_ms();
		equi_request_id(req->req_id, sizeof req->req_id);
		*con_cls = req;
		return MHD_YES;
	}

	struct http_request *req = *con_cls;
	http_set_current_req_id(req->req_id);

	if (*upload_data_size != 0) {
		if (!req->rejected && append_body(req, upload_data, *upload_data_size) != 0) {
			req->rejected = true;
			http_send_error(conn, 413, "Payload too large");
		}
		*upload_data_size = 0;
		return MHD_YES;
	}

	if (req->rejected) return MHD_YES;

	int rc = router_dispatch(s->router, req);
	return rc == MHD_NO ? MHD_NO : MHD_YES;
}

struct http_server *http_server_start(const struct config *cfg, struct http_router *router) {
	struct http_server *s = calloc(1, sizeof *s);
	if (!s) return NULL;
	s->router = router;

	struct sockaddr_in bind_addr;
	memset(&bind_addr, 0, sizeof bind_addr);
	bind_addr.sin_family = AF_INET;
	bind_addr.sin_port = htons(cfg->port);
	if (inet_pton(AF_INET, cfg->bind ? cfg->bind : "0.0.0.0", &bind_addr.sin_addr) != 1) {
		log_error("invalid bind address %s (expected IPv4, e.g. 0.0.0.0 or 127.0.0.1)", cfg->bind);
		free(s);
		return NULL;
	}

	unsigned flags = MHD_USE_AUTO_INTERNAL_THREAD | MHD_USE_ERROR_LOG | MHD_USE_TURBO;
	s->daemon = MHD_start_daemon(
		flags,
		cfg->port,
		NULL, NULL,
		&access_cb, s,
		MHD_OPTION_THREAD_POOL_SIZE, (unsigned int)cfg->thread_pool_size,
		MHD_OPTION_SOCK_ADDR, (struct sockaddr *)&bind_addr,
		MHD_OPTION_CONNECTION_LIMIT, (unsigned int)1024,
		MHD_OPTION_CONNECTION_TIMEOUT, (unsigned int)30,
		MHD_OPTION_LISTEN_BACKLOG_SIZE, (unsigned int)4096,
		MHD_OPTION_NOTIFY_COMPLETED, &request_completed_cb, NULL,
		MHD_OPTION_END);

	if (!s->daemon) {
		log_error("MHD_start_daemon failed (port=%u bind=%s): %s",
				  (unsigned)cfg->port, cfg->bind ? cfg->bind : "0.0.0.0", strerror(errno));
		free(s);
		return NULL;
	}
	return s;
}

void http_server_stop(struct http_server *s) {
	if (!s) return;
	if (s->daemon) MHD_stop_daemon(s->daemon);
	free(s);
}
