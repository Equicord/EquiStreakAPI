// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef EQUISTREAKAPI_LOG_H
#define EQUISTREAKAPI_LOG_H

#include <stdbool.h>

void log_init(bool silent, bool debug);

bool log_access_enabled(void);

void log_debug(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void log_info(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void log_warn(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void log_error(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

__attribute__((noreturn, format(printf, 1, 2))) void die(const char *fmt, ...);

void log_access(const char *method, const char *path, unsigned status,
				unsigned long long duration_ms, const char *req_id, const char *client_ip);

#endif
