// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _DEFAULT_SOURCE
#include "util.h"

#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/random.h>
#include <time.h>
#include <unistd.h>

void equi_iso_from_epoch(time_t t, char out[11]) {
	struct tm tm;
	gmtime_r(&t, &tm);
	strftime(out, 11, "%Y-%m-%d", &tm);
}

void equi_today_iso(char out[11]) {
	equi_iso_from_epoch(time(NULL), out);
}

void equi_yesterday_iso(char out[11]) {
	equi_iso_from_epoch(time(NULL) - 86400, out);
}

unsigned long long equi_now_ms(void) {
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
	return (unsigned long long)ts.tv_sec * 1000ULL + (unsigned long long)(ts.tv_nsec / 1000000);
}

void equi_rfc3339_now(char *out, size_t cap) {
	if (cap < 25) {
		if (cap > 0) out[0] = '\0';
		return;
	}
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	static __thread time_t tl_cached_sec = 0;
	static __thread char tl_cached_prefix[20];
	if (ts.tv_sec != tl_cached_sec) {
		struct tm tm;
		gmtime_r(&ts.tv_sec, &tm);
		snprintf(tl_cached_prefix, sizeof tl_cached_prefix,
				 "%04d-%02d-%02dT%02d:%02d:%02d",
				 tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
				 tm.tm_hour, tm.tm_min, tm.tm_sec);
		tl_cached_sec = ts.tv_sec;
	}
	int ms = (int)(ts.tv_nsec / 1000000);
	snprintf(out, cap, "%s.%03dZ", tl_cached_prefix, ms);
}

#define REQID_POOL_BYTES 512
#define REQID_BYTES 8
static __thread unsigned char tl_reqid_pool[REQID_POOL_BYTES];
static __thread size_t tl_reqid_off = REQID_POOL_BYTES;

static bool refill_reqid_pool(void) {
#ifdef __linux__
	ssize_t n = getrandom(tl_reqid_pool, REQID_POOL_BYTES, 0);
	if (n == REQID_POOL_BYTES) {
		tl_reqid_off = 0;
		return true;
	}
#endif
	int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
	if (fd >= 0) {
		ssize_t r = read(fd, tl_reqid_pool, REQID_POOL_BYTES);
		close(fd);
		if (r == REQID_POOL_BYTES) {
			tl_reqid_off = 0;
			return true;
		}
	}
	return false;
}

void equi_request_id(char *out, size_t cap) {
	if (cap < 17) {
		if (cap > 0) out[0] = '\0';
		return;
	}
	unsigned char buf[REQID_BYTES];
	if (tl_reqid_off + REQID_BYTES > REQID_POOL_BYTES) {
		if (!refill_reqid_pool()) {
			struct timespec ts;
			clock_gettime(CLOCK_MONOTONIC, &ts);
			uint64_t v = ((uint64_t)ts.tv_sec << 32) ^ (uint64_t)ts.tv_nsec ^
						 ((uint64_t)getpid() << 16) ^ (uint64_t)(uintptr_t)pthread_self();
			for (int i = 0; i < REQID_BYTES; i++)
				buf[i] = (unsigned char)(v >> (i * 8));
			equi_hex_lower(buf, sizeof buf, out);
			return;
		}
	}
	memcpy(buf, tl_reqid_pool + tl_reqid_off, REQID_BYTES);
	tl_reqid_off += REQID_BYTES;
	equi_hex_lower(buf, sizeof buf, out);
}
