// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#include "uid_set.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

void uid_set_free(struct uid_set *s) {
	if (s->slots) {
		for (size_t i = 0; i <= s->mask; i++)
			free(s->slots[i].id);
		free(s->slots);
	}
	s->slots = NULL;
	s->mask = s->count = 0;
}

static uint64_t fnv1a(const char *p, size_t n) {
	uint64_t h = 0xcbf29ce484222325ULL;
	for (size_t i = 0; i < n; i++) {
		h ^= (unsigned char)p[i];
		h *= 0x100000001b3ULL;
	}
	return h;
}

static int uid_set_rehash(struct uid_set *s, size_t new_cap) {
	struct uid_slot *old = s->slots;
	size_t old_mask = s->mask;
	struct uid_slot *ns = calloc(new_cap, sizeof *ns);
	if (!ns) return -1;
	s->slots = ns;
	s->mask = new_cap - 1;
	if (old) {
		for (size_t i = 0; i <= old_mask; i++) {
			if (!old[i].id) continue;
			size_t pos = fnv1a(old[i].id, old[i].len) & s->mask;
			while (s->slots[pos].id)
				pos = (pos + 1) & s->mask;
			s->slots[pos] = old[i];
		}
		free(old);
	}
	return 0;
}

int uid_set_add(struct uid_set *s, const char *id, size_t len) {
	if (!id || len == 0) return 0;
	if (!s->slots && uid_set_rehash(s, 64) != 0) return -1;
	if ((s->count + 1) * 2 > s->mask + 1) {
		if (uid_set_rehash(s, (s->mask + 1) * 2) != 0) return -1;
	}
	size_t pos = fnv1a(id, len) & s->mask;
	while (s->slots[pos].id) {
		if (s->slots[pos].len == len && memcmp(s->slots[pos].id, id, len) == 0) return 0;
		pos = (pos + 1) & s->mask;
	}
	char *copy = malloc(len + 1);
	if (!copy) return -1;
	memcpy(copy, id, len);
	copy[len] = '\0';
	s->slots[pos].id = copy;
	s->slots[pos].len = len;
	s->count++;
	return 0;
}
