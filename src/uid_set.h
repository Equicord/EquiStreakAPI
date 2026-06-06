// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef EQUISTREAKAPI_UID_SET_H
#define EQUISTREAKAPI_UID_SET_H

#include <stddef.h>

struct uid_slot {
	char *id;
	size_t len;
};

struct uid_set {
	struct uid_slot *slots;
	size_t mask;
	size_t count;
};

void uid_set_free(struct uid_set *s);
int uid_set_add(struct uid_set *s, const char *id, size_t len);

#endif
