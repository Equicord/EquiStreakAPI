// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#include "http_client.h"

#include <pthread.h>

static pthread_key_t g_curl_key;
static pthread_once_t g_curl_once = PTHREAD_ONCE_INIT;

static void curl_destructor(void *p) {
	if (p) curl_easy_cleanup(p);
}

static void make_curl_key(void) {
	pthread_key_create(&g_curl_key, curl_destructor);
}

CURL *http_client_get(void) {
	pthread_once(&g_curl_once, make_curl_key);
	CURL *c = pthread_getspecific(g_curl_key);
	if (c) {
		curl_easy_reset(c);
	} else {
		c = curl_easy_init();
		if (!c) return NULL;
		pthread_setspecific(g_curl_key, c);
	}
	curl_easy_setopt(c, CURLOPT_TCP_KEEPALIVE, 1L);
	curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(c, CURLOPT_TIMEOUT_MS, 5000L);
	return c;
}
