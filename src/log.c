// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#include "log.h"

#include "util.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool g_silent;
static bool g_debug;
static bool g_color;
static bool g_json;

static const char *C_RED = "";
static const char *C_YELLOW = "";
static const char *C_CYAN = "";
static const char *C_RESET = "";

void log_init(bool silent, bool debug) {
	g_silent = silent;
	g_debug = debug;

	const char *env = getenv("EQUISTREAKAPI_DEBUG");
	if (env && env[0] && strcmp(env, "0") != 0) {
		g_debug = true;
	}

	const char *fmt = getenv("LOG_FORMAT");
	g_json = (fmt && (strcmp(fmt, "json") == 0 || strcmp(fmt, "JSON") == 0));

	g_color = !g_json && isatty(STDERR_FILENO) && getenv("NO_COLOR") == NULL;
	if (g_color) {
		C_RED = "\033[31m";
		C_YELLOW = "\033[33m";
		C_CYAN = "\033[36m";
		C_RESET = "\033[0m";
	}
}

bool log_access_enabled(void) {
	return !g_silent;
}

static void emit_text(const char *prefix, const char *color, const char *fmt, va_list ap) {
	char ts[28];
	equi_rfc3339_now(ts, sizeof ts);
	flockfile(stderr);
	fprintf(stderr, "%s %s%s%s ", ts, color, prefix, C_RESET);
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	funlockfile(stderr);
}

static void emit_json(const char *level, const char *fmt, va_list ap) {
	char msg_raw[2048];
	int n = vsnprintf(msg_raw, sizeof msg_raw, fmt, ap);
	if (n < 0) msg_raw[0] = '\0';

	char msg_esc[4096];
	equi_json_escape(msg_raw, msg_esc, sizeof msg_esc);

	char ts[28];
	equi_rfc3339_now(ts, sizeof ts);

	flockfile(stderr);
	fprintf(stderr, "{\"ts\":\"%s\",\"level\":\"%s\",\"msg\":\"%s\"}\n", ts, level, msg_esc);
	funlockfile(stderr);
}

static void emit(const char *level, const char *prefix, const char *color, const char *fmt, va_list ap) {
	if (g_json)
		emit_json(level, fmt, ap);
	else
		emit_text(prefix, color, fmt, ap);
}

void log_debug(const char *fmt, ...) {
	if (!g_debug) return;
	va_list ap;
	va_start(ap, fmt);
	emit("debug", "[debug]", C_CYAN, fmt, ap);
	va_end(ap);
}

void log_info(const char *fmt, ...) {
	if (g_silent) return;
	va_list ap;
	va_start(ap, fmt);
	emit("info", "[info]", "", fmt, ap);
	va_end(ap);
}

void log_warn(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	emit("warn", "[warn]", C_YELLOW, fmt, ap);
	va_end(ap);
}

void log_error(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	emit("error", "[error]", C_RED, fmt, ap);
	va_end(ap);
}

void die(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	emit("error", "[error]", C_RED, fmt, ap);
	va_end(ap);
	exit(1);
}

void log_access(const char *method, const char *path, unsigned status,
				unsigned long long duration_ms, const char *req_id, const char *client_ip) {
	if (g_silent) return;
	if (!method) method = "?";
	if (!path) path = "/";
	if (!req_id) req_id = "-";
	if (!client_ip) client_ip = "-";
	if (g_json) {
		char path_esc[1024];
		equi_json_escape(path, path_esc, sizeof path_esc);
		char ts[28];
		equi_rfc3339_now(ts, sizeof ts);
		flockfile(stderr);
		fprintf(stderr,
				"{\"ts\":\"%s\",\"level\":\"info\",\"kind\":\"access\",\"req_id\":\"%s\",\"client_ip\":\"%s\",\"method\":\"%s\",\"path\":\"%s\",\"status\":%u,\"duration_ms\":%llu}\n",
				ts, req_id, client_ip, method, path_esc, status, duration_ms);
		funlockfile(stderr);
	} else {
		char ts[28];
		equi_rfc3339_now(ts, sizeof ts);
		flockfile(stderr);
		fprintf(stderr, "%s [access] %s %s %u %llums req_id=%s ip=%s\n",
				ts, method, path, status, duration_ms, req_id, client_ip);
		funlockfile(stderr);
	}
}
