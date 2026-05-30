// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#include "router.h"

#include "response.h"

#include <microhttpd.h>

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define MAX_ROUTE_PARAMS 8

struct route {
	char *method;
	char *pattern;
	int seg_count;
	char **segs;
	bool *is_param;
	http_handler_fn handler;
	void *userdata;
};

struct http_router {
	struct route *routes;
	size_t count;
	size_t cap;
	http_handler_fn fallback;
	void *fallback_userdata;
};

struct http_router *router_create(void) {
	struct http_router *r = calloc(1, sizeof *r);
	return r;
}

static void free_route(struct route *r) {
	free(r->method);
	free(r->pattern);
	for (int i = 0; i < r->seg_count; i++)
		free(r->segs[i]);
	free(r->segs);
	free(r->is_param);
}

void router_destroy(struct http_router *r) {
	if (!r) return;
	for (size_t i = 0; i < r->count; i++)
		free_route(&r->routes[i]);
	free(r->routes);
	free(r);
}

static int split_path(const char *path, char ***out_segs, bool **out_is_param) {
	while (*path == '/')
		path++;
	size_t cap = 4;
	char **segs = calloc(cap, sizeof *segs);
	bool *is_param = calloc(cap, sizeof *is_param);
	if (!segs || !is_param) {
		free(segs);
		free(is_param);
		return -1;
	}
	int n = 0;
	const char *p = path;
	while (*p) {
		const char *slash = strchr(p, '/');
		size_t len = slash ? (size_t)(slash - p) : strlen(p);
		if (len > 0) {
			if ((size_t)n + 1 > cap) {
				cap *= 2;
				char **ns = realloc(segs, cap * sizeof *ns);
				if (!ns) {
					for (int i = 0; i < n; i++)
						free(segs[i]);
					free(segs);
					free(is_param);
					return -1;
				}
				segs = ns;
				bool *nip = realloc(is_param, cap * sizeof *nip);
				if (!nip) {
					for (int i = 0; i < n; i++)
						free(segs[i]);
					free(segs);
					free(is_param);
					return -1;
				}
				is_param = nip;
			}
			bool is_p = (p[0] == ':');
			const char *src = is_p ? p + 1 : p;
			size_t slen = is_p ? len - 1 : len;
			segs[n] = malloc(slen + 1);
			if (!segs[n]) {
				for (int i = 0; i < n; i++)
					free(segs[i]);
				free(segs);
				free(is_param);
				return -1;
			}
			memcpy(segs[n], src, slen);
			segs[n][slen] = '\0';
			is_param[n] = is_p;
			n++;
		}
		if (!slash) break;
		p = slash + 1;
	}
	*out_segs = segs;
	*out_is_param = is_param;
	return n;
}

int router_add(struct http_router *r, const char *method, const char *path_pattern,
			   http_handler_fn handler, void *userdata) {
	if (r->count == r->cap) {
		size_t cap = r->cap ? r->cap * 2 : 8;
		struct route *nr = realloc(r->routes, cap * sizeof *nr);
		if (!nr) return -1;
		r->routes = nr;
		r->cap = cap;
	}
	struct route *rt = &r->routes[r->count];
	memset(rt, 0, sizeof *rt);
	rt->method = strdup(method);
	rt->pattern = strdup(path_pattern);
	if (!rt->method || !rt->pattern) goto fail;
	rt->seg_count = split_path(path_pattern, &rt->segs, &rt->is_param);
	if (rt->seg_count < 0) goto fail;
	rt->handler = handler;
	rt->userdata = userdata;
	r->count++;
	return 0;
fail:
	free_route(rt);
	memset(rt, 0, sizeof *rt);
	return -1;
}

static int match_path(struct route *rt, const char *path, struct http_request *req) {
	const char *p = path;
	while (*p == '/')
		p++;
	int idx = 0;
	while (*p) {
		const char *slash = strchr(p, '/');
		size_t len = slash ? (size_t)(slash - p) : strlen(p);
		if (len == 0) {
			if (!slash) break;
			p = slash + 1;
			continue;
		}
		if (idx >= rt->seg_count) return 0;
		if (rt->is_param[idx]) {
			if (req->route_param_count < MAX_ROUTE_PARAMS) {
				char *v = malloc(len + 1);
				if (!v) return 0;
				memcpy(v, p, len);
				v[len] = '\0';
				req->route_param_names[req->route_param_count] = rt->segs[idx];
				req->route_params[req->route_param_count] = v;
				req->route_param_count++;
			}
		} else {
			if (len != strlen(rt->segs[idx]) || strncmp(p, rt->segs[idx], len) != 0)
				return 0;
		}
		idx++;
		if (!slash) break;
		p = slash + 1;
	}
	return idx == rt->seg_count;
}

int router_dispatch(struct http_router *r, struct http_request *req) {
	if (strcmp(req->method, "OPTIONS") == 0) {
		return http_send_empty(req->conn, 204);
	}

	for (size_t i = 0; i < r->count; i++) {
		struct route *rt = &r->routes[i];
		if (strcmp(rt->method, req->method) != 0) continue;
		if (match_path(rt, req->path, req)) {
			return rt->handler(req, rt->userdata);
		}
		for (int j = 0; j < req->route_param_count; j++) {
			free(req->route_params[j]);
			req->route_params[j] = NULL;
			req->route_param_names[j] = NULL;
		}
		req->route_param_count = 0;
	}
	for (int j = 0; j < req->route_param_count; j++) {
		free(req->route_params[j]);
		req->route_params[j] = NULL;
	}
	req->route_param_count = 0;

	// Path exists with a different method → 405 with Allow header.
	char allow[128] = {0};
	for (size_t i = 0; i < r->count; i++) {
		struct route *rt = &r->routes[i];
		if (!match_path(rt, req->path, req)) {
			for (int j = 0; j < req->route_param_count; j++) {
				free(req->route_params[j]);
				req->route_params[j] = NULL;
				req->route_param_names[j] = NULL;
			}
			req->route_param_count = 0;
			continue;
		}
		for (int j = 0; j < req->route_param_count; j++) {
			free(req->route_params[j]);
			req->route_params[j] = NULL;
			req->route_param_names[j] = NULL;
		}
		req->route_param_count = 0;
		size_t cur = strlen(allow);
		size_t need = strlen(rt->method) + (cur ? 2 : 0);
		if (cur + need + 1 < sizeof allow) {
			if (cur) strcat(allow, ", ");
			strcat(allow, rt->method);
		}
	}
	if (allow[0]) {
		struct MHD_Response *resp = MHD_create_response_from_buffer(0, NULL, MHD_RESPMEM_PERSISTENT);
		if (resp) {
			MHD_add_response_header(resp, "Allow", allow);
			http_apply_default_headers(resp);
			int rc = MHD_queue_response(req->conn, 405, resp);
			MHD_destroy_response(resp);
			return rc;
		}
	}

	if (r->fallback) {
		return r->fallback(req, r->fallback_userdata);
	}
	return http_send_error(req->conn, 404, "Not Found");
}

void router_set_fallback(struct http_router *r, http_handler_fn handler, void *userdata) {
	r->fallback = handler;
	r->fallback_userdata = userdata;
}
