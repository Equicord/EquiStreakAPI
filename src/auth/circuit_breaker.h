// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef EQUISTREAKAPI_AUTH_CIRCUIT_BREAKER_H
#define EQUISTREAKAPI_AUTH_CIRCUIT_BREAKER_H

#include <stdbool.h>

bool breaker_allow(void);

void breaker_record_success(void);
void breaker_record_failure(void);

#define BREAKER_FAILURE_THRESHOLD 5
#define BREAKER_OPEN_MS 30000

#endif
