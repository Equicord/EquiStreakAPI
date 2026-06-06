// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef EQUISTREAKAPI_COUNTERS_H
#define EQUISTREAKAPI_COUNTERS_H

#include <stddef.h>

enum http_method_kind {
	METHOD_GET = 0,
	METHOD_POST,
	METHOD_OPTIONS,
	METHOD_OTHER,
	METHOD_COUNT,
};

enum http_status_class {
	STATUS_2XX = 0,
	STATUS_3XX,
	STATUS_4XX,
	STATUS_5XX,
	STATUS_CLASS_COUNT,
};

enum discord_result {
	DISCORD_OK = 0,
	DISCORD_ERROR,
	DISCORD_TIMEOUT,
	DISCORD_CIRCUIT_OPEN,
	DISCORD_RESULT_COUNT,
};

enum redis_result {
	REDIS_OP_OK = 0,
	REDIS_OP_ERR,
	REDIS_OP_COUNT,
};

enum cache_result {
	CACHE_HIT = 0,
	CACHE_MISS,
	CACHE_RESULT_COUNT,
};

enum http_method_kind method_kind_of(const char *method);
enum http_status_class status_class_of(unsigned status);

void counter_http_inc(enum http_method_kind m, enum http_status_class s);
void counter_discord_inc(enum discord_result r);
void counter_redis_inc(enum redis_result r);
void counter_cache_inc(enum cache_result r);

int counters_render_prometheus(char **out, size_t *out_len);

#endif
