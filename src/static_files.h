// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef EQUISTREAKAPI_STATIC_FILES_H
#define EQUISTREAKAPI_STATIC_FILES_H

#include <stdbool.h>
#include <stddef.h>

int static_files_init(const char *public_dir);
void static_files_shutdown(void);

struct http_request;
int static_files_fallback_handler(struct http_request *r, void *userdata);

#endif
