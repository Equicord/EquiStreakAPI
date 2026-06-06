// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#include "static_files.h"

#include "http/request.h"
#include "http/response.h"
#include "log.h"
#include "util.h"

#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

struct file_entry {
	char *path;
	char *body;
	size_t len;
	const char *mime;
};

static struct file_entry *g_files = NULL;
static size_t g_count = 0;
static size_t g_cap = 0;

static const char *mime_for(const char *name) {
	const char *dot = strrchr(name, '.');
	if (!dot) return "application/octet-stream";
	if (!strcmp(dot, ".html") || !strcmp(dot, ".htm")) return "text/html; charset=utf-8";
	if (!strcmp(dot, ".css")) return "text/css; charset=utf-8";
	if (!strcmp(dot, ".js")) return "application/javascript; charset=utf-8";
	if (!strcmp(dot, ".json")) return "application/json";
	if (!strcmp(dot, ".svg")) return "image/svg+xml";
	if (!strcmp(dot, ".png")) return "image/png";
	if (!strcmp(dot, ".jpg") || !strcmp(dot, ".jpeg")) return "image/jpeg";
	if (!strcmp(dot, ".webp")) return "image/webp";
	if (!strcmp(dot, ".ico")) return "image/x-icon";
	if (!strcmp(dot, ".txt")) return "text/plain; charset=utf-8";
	return "application/octet-stream";
}

static int add_file(const char *web_path, char *body, size_t len) {
	if (g_count == g_cap) {
		size_t cap = g_cap ? g_cap * 2 : 8;
		struct file_entry *p = realloc(g_files, cap * sizeof *p);
		if (!p) return -1;
		g_files = p;
		g_cap = cap;
	}
	g_files[g_count].path = strdup(web_path);
	g_files[g_count].body = body;
	g_files[g_count].len = len;
	g_files[g_count].mime = mime_for(web_path);
	if (!g_files[g_count].path) return -1;
	g_count++;
	return 0;
}

static int walk_dir(const char *dir, const char *web_prefix) {
	DIR *d = opendir(dir);
	if (!d) return -1;
	struct dirent *e;
	int rc = 0;
	while ((e = readdir(d)) != NULL) {
		if (e->d_name[0] == '.') continue;
		char *full = NULL;
		char *web = NULL;
		if (equi_xasprintf(&full, "%s/%s", dir, e->d_name) != 0) {
			rc = -1;
			break;
		}
		if (equi_xasprintf(&web, "%s/%s", web_prefix, e->d_name) != 0) {
			free(full);
			rc = -1;
			break;
		}
		struct stat st;
		if (stat(full, &st) == 0) {
			if (S_ISDIR(st.st_mode)) {
				walk_dir(full, web);
			} else if (S_ISREG(st.st_mode)) {
				char *body = NULL;
				size_t len = 0;
				if (equi_read_file(full, 8u << 20, &body, &len) == 0) {
					if (add_file(web, body, len) != 0) {
						free(body);
					}
				} else {
					log_warn("static: failed to read %s", full);
				}
			}
		}
		free(full);
		free(web);
	}
	closedir(d);
	return rc;
}

int static_files_init(const char *public_dir) {
	if (!public_dir) return -1;
	struct stat st;
	if (stat(public_dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
		return -1;
	}
	return walk_dir(public_dir, "");
}

void static_files_shutdown(void) {
	for (size_t i = 0; i < g_count; i++) {
		free(g_files[i].path);
		free(g_files[i].body);
	}
	free(g_files);
	g_files = NULL;
	g_count = g_cap = 0;
}

static bool static_files_get(const char *path, const void **out_body, size_t *out_len, const char **out_mime) {
	if (!path) return false;
	const char *lookup = path;
	if (strcmp(path, "/") == 0) lookup = "/index.html";
	for (size_t i = 0; i < g_count; i++) {
		if (strcmp(g_files[i].path, lookup) == 0) {
			*out_body = g_files[i].body;
			*out_len = g_files[i].len;
			*out_mime = g_files[i].mime;
			return true;
		}
	}
	return false;
}

static int static_files_try_serve(struct MHD_Connection *conn, const char *path) {
	const void *body;
	size_t len;
	const char *mime;
	if (!static_files_get(path, &body, &len, &mime)) return 0;
	return http_send_static(conn, 200, mime, body, len);
}

int static_files_fallback_handler(struct http_request *r, void *userdata) {
	(void)userdata;
	if (strcmp(r->method, "GET") != 0 && strcmp(r->method, "HEAD") != 0)
		return http_send_error(r->conn, 404, "Not Found");
	int rc = static_files_try_serve(r->conn, r->path);
	if (rc) return rc;
	return http_send_error(r->conn, 404, "Not Found");
}
