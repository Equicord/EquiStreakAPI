// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef EQUISTREAKAPI_ROUTES_STREAKS_INTERNAL_H
#define EQUISTREAKAPI_ROUTES_STREAKS_INTERNAL_H

#include "../http/request.h"

#define MAX_UID_LEN 64
#define MAX_MIGRATE_RECIPIENTS 1000

int require_bearer(struct http_request *r, char *out_uid, size_t cap, int *err_rc);
void sort_two_ids(const char *a, const char *b, const char **lo, const char **hi);

int handle_admin_update(struct http_request *r, void *userdata);
int handle_migrate(struct http_request *r, void *userdata);

#endif
