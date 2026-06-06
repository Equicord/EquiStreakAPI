// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef EQUISTREAKAPI_AUTH_DISCORD_H
#define EQUISTREAKAPI_AUTH_DISCORD_H

#include <stddef.h>

int discord_get_user_id(const char *bearer_token, char *out_id, size_t cap);

#endif
