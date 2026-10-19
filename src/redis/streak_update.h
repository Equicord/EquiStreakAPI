// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef EQUISTREAKAPI_REDIS_STREAK_UPDATE_H
#define EQUISTREAKAPI_REDIS_STREAK_UPDATE_H

#include <stdbool.h>
#include <stdint.h>

struct equi_buf;

enum streak_pending {
	STREAK_PENDING_NONE = 0,
	STREAK_PENDING_A = 1,
	STREAK_PENDING_B = 2,
};

struct streak_state {
	long count;
	int64_t last_round_ts;
	int64_t pending_ts;
	enum streak_pending pending;
	char last_streak_date[11];
	char pending_date[11];
	char timezone_a[129];
	char timezone_b[129];
	char activity_a_date[11];
	char activity_b_date[11];
	char today_a_date[11];
	char today_b_date[11];
	char completed_a_date[11];
	char completed_b_date[11];
	int64_t activity_a_ts;
	int64_t activity_b_ts;
	bool user_a_today;
	bool user_b_today;
};

struct streak_decision {
	bool wrote;
	struct streak_state state;
};

#define STREAK_WINDOW_S_DEFAULT ((int64_t)24 * 60 * 60)

void streak_apply(int64_t now, int64_t window_s,
				  const char *current_uid,
				  const char *message_author_id,
				  const char *user_a_id, const char *user_b_id,
				  const char *reported_timezone,
				  const struct streak_state *in, bool in_present,
				  struct streak_decision *out);

int streak_update(const char *current_uid, const char *user_a_id, const char *user_b_id,
				  const char *message_author_id, const char *timezone,
				  struct equi_buf *out);

#endif
