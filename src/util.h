// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef EQUISTREAKAPI_UTIL_H
#define EQUISTREAKAPI_UTIL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

int equi_xasprintf(char **out, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

struct equi_buf {
	char *data;
	size_t len;
	size_t cap;
};

int equi_buf_grow(struct equi_buf *b, size_t need);
int equi_buf_putn(struct equi_buf *b, const void *s, size_t n);
int equi_buf_puts(struct equi_buf *b, const char *s);
int equi_buf_putc(struct equi_buf *b, char c);
int equi_buf_printf(struct equi_buf *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void equi_buf_free(struct equi_buf *b);

void equi_json_escape(const char *in, char *out, size_t cap);

int equi_read_file(const char *path, size_t max_bytes, char **out, size_t *out_len);

void equi_today_iso(char out[11]);
void equi_yesterday_iso(char out[11]);
void equi_iso_from_epoch(time_t t, char out[11]);

bool equi_const_time_eq(const char *a, const char *b);

void equi_hex_lower(const unsigned char *in, size_t n, char *out);

void equi_url_encode(const char *in, struct equi_buf *out);

size_t equi_buf_curl_write(void *ptr, size_t size, size_t nmemb, void *userdata);

bool equi_is_discord_id(const char *s);
bool equi_is_safe_token(const char *s);

void equi_request_id(char *out, size_t cap);

unsigned long long equi_now_ms(void);

void equi_rfc3339_now(char *out, size_t cap);

struct json_object *j_obj(void);
struct json_object *j_arr(void);
struct json_object *j_str(const char *s);
struct json_object *j_strn(const char *s, size_t n);
struct json_object *j_int(long long v);
struct json_object *j_bool(bool v);
void j_add(struct json_object *parent, const char *key, struct json_object *child);
void j_arr_add(struct json_object *parent, struct json_object *child);

bool j_get_str(struct json_object *obj, const char *key, const char **out);
bool j_get_int64(struct json_object *obj, const char *key, int64_t *out);
bool j_get_bool(struct json_object *obj, const char *key, bool *out);

#endif
