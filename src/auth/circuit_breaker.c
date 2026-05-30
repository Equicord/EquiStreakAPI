// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#include "circuit_breaker.h"

#include "../log.h"
#include "../util.h"

#include <stdatomic.h>

enum breaker_state {
	BREAKER_CLOSED = 0,
	BREAKER_OPEN,
	BREAKER_HALF_OPEN,
};

static _Atomic int g_state = BREAKER_CLOSED;
static _Atomic int g_failures;
static _Atomic unsigned long long g_open_until_ms;

bool breaker_allow(void) {
	int state = atomic_load_explicit(&g_state, memory_order_acquire);
	if (state == BREAKER_CLOSED) return true;

	unsigned long long now = equi_now_ms();
	unsigned long long open_until = atomic_load_explicit(&g_open_until_ms, memory_order_acquire);
	if (now >= open_until) {
		int expected = BREAKER_OPEN;
		if (atomic_compare_exchange_strong_explicit(&g_state, &expected, BREAKER_HALF_OPEN,
													memory_order_acq_rel, memory_order_acquire)) {
			log_info("breaker: half-open trial");
		}
		return true;
	}
	return false;
}

void breaker_record_success(void) {
	int prev = atomic_exchange_explicit(&g_state, BREAKER_CLOSED, memory_order_acq_rel);
	atomic_store_explicit(&g_failures, 0, memory_order_release);
	if (prev != BREAKER_CLOSED) {
		log_info("breaker: closed");
	}
}

void breaker_record_failure(void) {
	int prev_state = atomic_load_explicit(&g_state, memory_order_acquire);
	if (prev_state == BREAKER_HALF_OPEN) {
		atomic_store_explicit(&g_open_until_ms, equi_now_ms() + BREAKER_OPEN_MS, memory_order_release);
		atomic_store_explicit(&g_state, BREAKER_OPEN, memory_order_release);
		log_warn("breaker: re-opened after half-open trial failure");
		return;
	}
	int n = atomic_fetch_add_explicit(&g_failures, 1, memory_order_acq_rel) + 1;
	if (n >= BREAKER_FAILURE_THRESHOLD) {
		int expected = BREAKER_CLOSED;
		if (atomic_compare_exchange_strong_explicit(&g_state, &expected, BREAKER_OPEN,
													memory_order_acq_rel, memory_order_acquire)) {
			atomic_store_explicit(&g_open_until_ms, equi_now_ms() + BREAKER_OPEN_MS, memory_order_release);
			log_warn("breaker: opened after %d consecutive failures", n);
		}
	}
}
