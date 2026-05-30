// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#include "counters.h"

#include "util.h"

#include <stdalign.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CL 64
alignas(CL) static _Atomic unsigned long long c_http[METHOD_COUNT][STATUS_CLASS_COUNT];
alignas(CL) static _Atomic unsigned long long c_discord[DISCORD_RESULT_COUNT];
alignas(CL) static _Atomic unsigned long long c_redis[REDIS_OP_COUNT];
alignas(CL) static _Atomic unsigned long long c_cache[CACHE_RESULT_COUNT];

static const char *method_names[METHOD_COUNT] = {"GET", "POST", "OPTIONS", "other"};
static const char *status_names[STATUS_CLASS_COUNT] = {"2xx", "3xx", "4xx", "5xx"};
static const char *discord_names[DISCORD_RESULT_COUNT] = {"ok", "error", "timeout", "circuit_open"};
static const char *redis_names[REDIS_OP_COUNT] = {"ok", "error"};
static const char *cache_names[CACHE_RESULT_COUNT] = {"hit", "miss"};

enum http_method_kind method_kind_of(const char *method) {
	if (!method) return METHOD_OTHER;
	if (!strcmp(method, "GET")) return METHOD_GET;
	if (!strcmp(method, "POST")) return METHOD_POST;
	if (!strcmp(method, "OPTIONS")) return METHOD_OPTIONS;
	return METHOD_OTHER;
}

enum http_status_class status_class_of(unsigned status) {
	if (status >= 500) return STATUS_5XX;
	if (status >= 400) return STATUS_4XX;
	if (status >= 300) return STATUS_3XX;
	return STATUS_2XX;
}

void counter_http_inc(enum http_method_kind m, enum http_status_class s) {
	if (m >= METHOD_COUNT || s >= STATUS_CLASS_COUNT) return;
	atomic_fetch_add_explicit(&c_http[m][s], 1, memory_order_relaxed);
}

void counter_discord_inc(enum discord_result r) {
	if (r >= DISCORD_RESULT_COUNT) return;
	atomic_fetch_add_explicit(&c_discord[r], 1, memory_order_relaxed);
}

void counter_redis_inc(enum redis_result r) {
	if (r >= REDIS_OP_COUNT) return;
	atomic_fetch_add_explicit(&c_redis[r], 1, memory_order_relaxed);
}

void counter_cache_inc(enum cache_result r) {
	if (r >= CACHE_RESULT_COUNT) return;
	atomic_fetch_add_explicit(&c_cache[r], 1, memory_order_relaxed);
}

int counters_render_prometheus(char **out, size_t *out_len) {
	struct equi_buf b = {0};
	if (equi_buf_grow(&b, 4096) != 0) goto fail;

	if (equi_buf_puts(&b, "# HELP equi_http_requests_total Total HTTP requests\n# TYPE equi_http_requests_total counter\n") != 0) goto fail;
	for (int m = 0; m < METHOD_COUNT; m++) {
		for (int s = 0; s < STATUS_CLASS_COUNT; s++) {
			unsigned long long v = atomic_load_explicit(&c_http[m][s], memory_order_relaxed);
			if (equi_buf_printf(&b, "equi_http_requests_total{method=\"%s\",status=\"%s\"} %llu\n",
								method_names[m], status_names[s], v) != 0) goto fail;
		}
	}

	if (equi_buf_puts(&b, "# HELP equi_discord_calls_total Outbound Discord calls\n# TYPE equi_discord_calls_total counter\n") != 0) goto fail;
	for (int r = 0; r < DISCORD_RESULT_COUNT; r++) {
		unsigned long long v = atomic_load_explicit(&c_discord[r], memory_order_relaxed);
		if (equi_buf_printf(&b, "equi_discord_calls_total{result=\"%s\"} %llu\n",
							discord_names[r], v) != 0) goto fail;
	}

	if (equi_buf_puts(&b, "# HELP equi_redis_ops_total Redis command outcomes\n# TYPE equi_redis_ops_total counter\n") != 0) goto fail;
	for (int r = 0; r < REDIS_OP_COUNT; r++) {
		unsigned long long v = atomic_load_explicit(&c_redis[r], memory_order_relaxed);
		if (equi_buf_printf(&b, "equi_redis_ops_total{result=\"%s\"} %llu\n",
							redis_names[r], v) != 0) goto fail;
	}

	if (equi_buf_puts(&b, "# HELP equi_auth_cache_total Auth cache lookups\n# TYPE equi_auth_cache_total counter\n") != 0) goto fail;
	for (int r = 0; r < CACHE_RESULT_COUNT; r++) {
		unsigned long long v = atomic_load_explicit(&c_cache[r], memory_order_relaxed);
		if (equi_buf_printf(&b, "equi_auth_cache_total{result=\"%s\"} %llu\n",
							cache_names[r], v) != 0) goto fail;
	}

	*out = b.data;
	*out_len = b.len;
	return 0;
fail:
	equi_buf_free(&b);
	*out = NULL;
	*out_len = 0;
	return -1;
}
