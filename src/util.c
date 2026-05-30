// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#include "util.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int equi_xasprintf(char **out, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	va_list ap2;
	va_copy(ap2, ap);
	int n = vsnprintf(NULL, 0, fmt, ap);
	va_end(ap);
	if (n < 0) {
		va_end(ap2);
		*out = NULL;
		return -1;
	}
	*out = malloc((size_t)n + 1);
	if (!*out) {
		va_end(ap2);
		return -1;
	}
	vsnprintf(*out, (size_t)n + 1, fmt, ap2);
	va_end(ap2);
	return 0;
}

int equi_buf_grow(struct equi_buf *b, size_t need) {
	if (b->cap >= need) return 0;
	size_t cap = b->cap ? b->cap : 256;
	while (cap < need)
		cap *= 2;
	char *p = realloc(b->data, cap);
	if (!p) return -1;
	b->data = p;
	b->cap = cap;
	return 0;
}

int equi_buf_putn(struct equi_buf *b, const void *s, size_t n) {
	if (equi_buf_grow(b, b->len + n + 1) != 0) return -1;
	memcpy(b->data + b->len, s, n);
	b->len += n;
	b->data[b->len] = '\0';
	return 0;
}

int equi_buf_puts(struct equi_buf *b, const char *s) {
	return equi_buf_putn(b, s, strlen(s));
}

int equi_buf_putc(struct equi_buf *b, char c) {
	return equi_buf_putn(b, &c, 1);
}

int equi_buf_printf(struct equi_buf *b, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	va_list ap2;
	va_copy(ap2, ap);
	int n = vsnprintf(NULL, 0, fmt, ap);
	va_end(ap);
	if (n < 0) {
		va_end(ap2);
		return -1;
	}
	if (equi_buf_grow(b, b->len + (size_t)n + 1) != 0) {
		va_end(ap2);
		return -1;
	}
	vsnprintf(b->data + b->len, (size_t)n + 1, fmt, ap2);
	va_end(ap2);
	b->len += (size_t)n;
	return 0;
}

void equi_buf_free(struct equi_buf *b) {
	free(b->data);
	b->data = NULL;
	b->len = b->cap = 0;
}

int equi_read_file(const char *path, size_t max_bytes, char **out, size_t *out_len) {
	if (!path || !out || !out_len) return -1;
	*out = NULL;
	*out_len = 0;
	FILE *f = fopen(path, "rb");
	if (!f) return -1;
	if (fseek(f, 0, SEEK_END) != 0) {
		fclose(f);
		return -1;
	}
	long ssz = ftell(f);
	if (ssz < 0) {
		fclose(f);
		return -1;
	}
	size_t sz = (size_t)ssz;
	if (max_bytes && sz > max_bytes) {
		fclose(f);
		errno = EFBIG;
		return -1;
	}
	if (fseek(f, 0, SEEK_SET) != 0) {
		fclose(f);
		return -1;
	}
	char *buf = malloc(sz + 1);
	if (!buf) {
		fclose(f);
		return -1;
	}
	size_t got = fread(buf, 1, sz, f);
	int err = ferror(f);
	fclose(f);
	if (err || got != sz) {
		free(buf);
		return -1;
	}
	buf[sz] = '\0';
	*out = buf;
	*out_len = sz;
	return 0;
}

bool equi_const_time_eq(const char *a, const char *b) {
	if (!a || !b) return false;
	size_t la = strlen(a);
	size_t lb = strlen(b);
	enum { WINDOW = 256 };
	unsigned char diff = (unsigned char)((la ^ lb) & 0xff) | (unsigned char)(((la ^ lb) >> 8) & 0xff);
	for (size_t i = 0; i < WINDOW; i++) {
		unsigned char av = (i < la) ? (unsigned char)a[i] : 0;
		unsigned char bv = (i < lb) ? (unsigned char)b[i] : 0;
		diff |= (unsigned char)(av ^ bv);
	}
	return diff == 0 && la == lb;
}

void equi_hex_lower(const unsigned char *in, size_t n, char *out) {
	static const char hex[] = "0123456789abcdef";
	for (size_t i = 0; i < n; i++) {
		out[i * 2] = hex[in[i] >> 4];
		out[i * 2 + 1] = hex[in[i] & 0x0f];
	}
	out[n * 2] = '\0';
}

size_t equi_buf_curl_write(void *ptr, size_t size, size_t nmemb, void *userdata) {
	struct equi_buf *b = userdata;
	size_t total = size * nmemb;
	if (equi_buf_putn(b, ptr, total) != 0) return 0;
	return total;
}

bool equi_is_discord_id(const char *s) {
	if (!s || !*s) return false;
	size_t n = 0;
	for (const unsigned char *p = (const unsigned char *)s; *p; p++, n++) {
		if (n >= 20) return false;
		if (*p < '0' || *p > '9') return false;
	}
	return n >= 1;
}

bool equi_is_safe_token(const char *s) {
	if (!s || !*s) return false;
	size_t n = 0;
	for (const unsigned char *p = (const unsigned char *)s; *p; p++, n++) {
		if (n >= 512) return false;
		if (*p < 0x20 || *p == 0x7f) return false;
	}
	return n >= 1;
}

void equi_url_encode(const char *in, struct equi_buf *out) {
	if (!in) return;
	for (const unsigned char *p = (const unsigned char *)in; *p; p++) {
		unsigned char c = *p;
		bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~';
		if (unreserved) {
			equi_buf_putc(out, (char)c);
		} else {
			char tmp[4];
			snprintf(tmp, sizeof tmp, "%%%02X", c);
			equi_buf_puts(out, tmp);
		}
	}
}
