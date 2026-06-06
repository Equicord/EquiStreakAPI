// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#include "util.h"

#include <json-c/json.h>

#include <stdio.h>
#include <string.h>

void equi_json_escape(const char *in, char *out, size_t cap) {
	if (cap == 0) return;
	if (!in) {
		out[0] = '\0';
		return;
	}
	size_t n = 0;
	for (const unsigned char *p = (const unsigned char *)in; *p && n + 8 < cap; p++) {
		unsigned char c = *p;
		if (c == '"' || c == '\\') {
			out[n++] = '\\';
			out[n++] = (char)c;
		} else if (c == '\n') {
			out[n++] = '\\';
			out[n++] = 'n';
		} else if (c == '\r') {
			out[n++] = '\\';
			out[n++] = 'r';
		} else if (c == '\t') {
			out[n++] = '\\';
			out[n++] = 't';
		} else if (c < 0x20) {
			int w = snprintf(out + n, cap - n, "\\u%04x", c);
			if (w > 0) n += (size_t)w;
		} else {
			out[n++] = (char)c;
		}
	}
	out[n] = '\0';
}

struct json_object *j_obj(void) {
	return json_object_new_object();
}

struct json_object *j_arr(void) {
	return json_object_new_array();
}

struct json_object *j_str(const char *s) {
	return s ? json_object_new_string(s) : NULL;
}

struct json_object *j_strn(const char *s, size_t n) {
	return s ? json_object_new_string_len(s, (int)n) : NULL;
}

struct json_object *j_int(long long v) {
	return json_object_new_int64((int64_t)v);
}

struct json_object *j_bool(bool v) {
	return json_object_new_boolean(v ? 1 : 0);
}

void j_add(struct json_object *parent, const char *key, struct json_object *child) {
	if (!parent || !key) {
		if (child) json_object_put(child);
		return;
	}
	json_object_object_add(parent, key, child);
}

void j_arr_add(struct json_object *parent, struct json_object *child) {
	if (!parent) {
		if (child) json_object_put(child);
		return;
	}
	json_object_array_add(parent, child);
}

bool j_get_str(struct json_object *obj, const char *key, const char **out) {
	struct json_object *v = NULL;
	if (!obj || !key || !out) return false;
	if (!json_object_object_get_ex(obj, key, &v) || !v) return false;
	if (!json_object_is_type(v, json_type_string)) return false;
	*out = json_object_get_string(v);
	return true;
}

bool j_get_int64(struct json_object *obj, const char *key, int64_t *out) {
	struct json_object *v = NULL;
	if (!obj || !key || !out) return false;
	if (!json_object_object_get_ex(obj, key, &v) || !v) return false;
	if (!json_object_is_type(v, json_type_int) && !json_object_is_type(v, json_type_double))
		return false;
	*out = (int64_t)json_object_get_int64(v);
	return true;
}

bool j_get_bool(struct json_object *obj, const char *key, bool *out) {
	struct json_object *v = NULL;
	if (!obj || !key || !out) return false;
	if (!json_object_object_get_ex(obj, key, &v) || !v) return false;
	*out = json_object_get_boolean(v) ? true : false;
	return true;
}
