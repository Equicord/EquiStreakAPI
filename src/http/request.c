// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#include "request.h"

#include <json-c/json.h>
#include <microhttpd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

const char *http_req_header(struct http_request *r, const char *name) {
	return MHD_lookup_connection_value(r->conn, MHD_HEADER_KIND, name);
}

const char *http_req_query(struct http_request *r, const char *name) {
	return MHD_lookup_connection_value(r->conn, MHD_GET_ARGUMENT_KIND, name);
}

const char *http_req_param(struct http_request *r, const char *name) {
	for (int i = 0; i < r->route_param_count; i++) {
		if (r->route_param_names[i] && strcmp(r->route_param_names[i], name) == 0)
			return r->route_params[i];
	}
	return NULL;
}

struct json_object *http_req_json(struct http_request *r) {
	if (!r->body || r->body_len == 0) return NULL;
	struct json_tokener *tok = json_tokener_new();
	if (!tok) return NULL;
	struct json_object *obj = json_tokener_parse_ex(tok, r->body, (int)r->body_len);
	json_tokener_free(tok);
	return obj;
}

static void format_sockaddr(const struct sockaddr *sa, char *out, size_t cap) {
	if (!sa || !out || cap == 0) return;
	out[0] = '\0';
	if (sa->sa_family == AF_INET) {
		const struct sockaddr_in *in = (const struct sockaddr_in *)sa;
		inet_ntop(AF_INET, &in->sin_addr, out, (socklen_t)cap);
	} else if (sa->sa_family == AF_INET6) {
		const struct sockaddr_in6 *in6 = (const struct sockaddr_in6 *)sa;
		inet_ntop(AF_INET6, &in6->sin6_addr, out, (socklen_t)cap);
	}
}

static bool g_trust_xff = false;

void http_set_trust_xff(bool trust) {
	g_trust_xff = trust;
}

const char *http_req_client_ip(struct http_request *r) {
	if (!r) return "";
	if (r->client_ip[0]) return r->client_ip;

	if (g_trust_xff) {
		const char *xff = http_req_header(r, "X-Forwarded-For");
		if (xff && xff[0]) {

			const char *last = xff;
			for (const char *p = xff; *p; p++) {
				if (*p == ',') {
					const char *cand = p + 1;
					while (*cand == ' ')
						cand++;
					if (*cand) last = cand;
				}
			}
			size_t n = 0;
			while (last[n] && last[n] != ',' && last[n] != ' ' && n < sizeof r->client_ip - 1) {
				r->client_ip[n] = last[n];
				n++;
			}
			r->client_ip[n] = '\0';
			if (r->client_ip[0]) return r->client_ip;
		}
	}

	const union MHD_ConnectionInfo *ci =
		MHD_get_connection_info(r->conn, MHD_CONNECTION_INFO_CLIENT_ADDRESS);
	if (ci && ci->client_addr) {
		format_sockaddr(ci->client_addr, r->client_ip, sizeof r->client_ip);
	}
	return r->client_ip;
}
