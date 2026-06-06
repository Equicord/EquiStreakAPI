// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef EQUISTREAKAPI_REDIS_STREAK_UPDATE_H
#define EQUISTREAKAPI_REDIS_STREAK_UPDATE_H

#include <stdbool.h>
#include <stdint.h>

struct equi_buf;

struct streak_state {
	long count;
	char last_streak_date[16];
	char today_date[16];
	bool user_a_today;
	bool user_b_today;
};

struct streak_decision {
	bool wrote;
	struct streak_state state;
};

void streak_apply(const char *today_iso, const char *yesterday_iso,
				  const char *current_uid, const char *user_a_id, const char *user_b_id,
				  const struct streak_state *in, bool in_present,
				  struct streak_decision *out);

int streak_update(const char *current_uid, const char *user_a_id, const char *user_b_id,
				  struct equi_buf *out);

#endif
