// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef EQUISTREAKAPI_REDIS_PIPELINE_H
#define EQUISTREAKAPI_REDIS_PIPELINE_H

#include <hiredis/hiredis.h>

#include <stdbool.h>
#include <stddef.h>

struct equi_buf;

int redis_reply_streak_append_json(redisReply *r, struct equi_buf *out);

static inline void redis_reply_safe_free(redisReply *r) {
	if (r) freeReplyObject(r);
}

int redis_streak_key(const char *lo, const char *hi, char *buf, size_t cap);

int redis_streak_register_pair(redisContext *c, const char *lo, const char *hi, const char *key);

int redis_streak_append_hmset(redisContext *c,
							  const char *key, size_t key_len,
							  long count,
							  const char *lo, size_t lo_len,
							  const char *hi, size_t hi_len,
							  const char *last_streak_date,
							  const char *today_date,
							  bool user_a_today, bool user_b_today);

#endif
