// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef EQUISTREAKAPI_REDIS_H
#define EQUISTREAKAPI_REDIS_H

#include <hiredis/hiredis.h>

#include <stdbool.h>
#include <stddef.h>

int redis_init(const char *url);
void redis_shutdown(void);

redisContext *redis_ctx(void);
void redis_invalidate_ctx(void);

bool redis_reply_is_error(redisReply *r);

char *redis_hget_str(const char *key, const char *field);
bool redis_ping_ok(void);

struct MHD_Connection;
redisContext *redis_ctx_or_503(struct MHD_Connection *conn);

void redis_url_redact(const char *url, char *out, size_t cap);

#endif
