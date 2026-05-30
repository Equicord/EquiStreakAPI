// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#include "oauth.h"

#include "../config.h"
#include "../util.h"
#include "http_client.h"

#include <curl/curl.h>

int oauth_exchange_code(const struct config *cfg, const char *code,
						char **out_response, size_t *out_len, long *out_http_code) {
	if (!cfg || !code || !out_response || !out_len) return -1;
	*out_response = NULL;
	*out_len = 0;
	if (out_http_code) *out_http_code = 0;

	struct equi_buf form = {0};
	equi_buf_puts(&form, "client_id=");
	equi_url_encode(cfg->discord_client_id, &form);
	equi_buf_puts(&form, "&client_secret=");
	equi_url_encode(cfg->discord_client_secret, &form);
	equi_buf_puts(&form, "&grant_type=authorization_code&code=");
	equi_url_encode(code, &form);
	equi_buf_puts(&form, "&redirect_uri=");
	equi_url_encode(cfg->discord_redirect_uri, &form);

	if (!form.data) return -1;

	CURL *curl = http_client_get();
	if (!curl) {
		equi_buf_free(&form);
		return -1;
	}

	struct curl_slist *headers = NULL;
	headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");
	headers = curl_slist_append(headers, "User-Agent: equistreakapi/0.1.0");

	struct equi_buf body = {0};

	curl_easy_setopt(curl, CURLOPT_URL, "https://discord.com/api/oauth2/token");
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_POST, 1L);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, form.data);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)form.len);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, equi_buf_curl_write);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);

	CURLcode rc = curl_easy_perform(curl);
	long http_code = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

	curl_slist_free_all(headers);
	equi_buf_free(&form);

	if (out_http_code) *out_http_code = http_code;
	if (rc != CURLE_OK) {
		equi_buf_free(&body);
		return -1;
	}

	*out_response = body.data;
	*out_len = body.len;
	body.data = NULL;
	body.cap = body.len = 0;
	return 0;
}
