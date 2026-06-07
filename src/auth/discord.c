// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#include "discord.h"

#include "../counters.h"
#include "../log.h"
#include "../util.h"
#include "circuit_breaker.h"
#include "http_client.h"

#include <curl/curl.h>
#include <json-c/json.h>

#include <stdio.h>
#include <string.h>

int discord_get_user_id(const char *bearer_token, char *out_id, size_t cap) {
	if (!bearer_token || !out_id || cap == 0) return -1;
	if (!equi_is_safe_token(bearer_token)) return -1;
	out_id[0] = '\0';

	if (!breaker_allow()) {
		counter_discord_inc(DISCORD_CIRCUIT_OPEN);
		return -1;
	}

	CURL *curl = http_client_get();
	if (!curl) return -1;

	char auth_header[2048];
	int n = snprintf(auth_header, sizeof auth_header, "Authorization: Bearer %s", bearer_token);
	if (n < 0 || n >= (int)sizeof auth_header) return -1;

	struct curl_slist *headers = NULL;
	headers = curl_slist_append(headers, auth_header);
	headers = curl_slist_append(headers, "User-Agent: equistreakapi/0.1.0");

	struct equi_buf body = {0};
	curl_easy_setopt(curl, CURLOPT_URL, "https://discord.com/api/v10/users/@me");
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, equi_buf_curl_write);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);

	CURLcode rc = curl_easy_perform(curl);
	long http_code = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
	curl_slist_free_all(headers);

	if (rc != CURLE_OK || http_code != 200 || !body.data) {
		log_warn("discord /users/@me failed (curl=%d http=%ld)", (int)rc, http_code);
		counter_discord_inc(rc == CURLE_OPERATION_TIMEDOUT ? DISCORD_TIMEOUT : DISCORD_ERROR);
		breaker_record_failure();
		equi_buf_free(&body);
		return http_code > 0 ? (int)http_code : -1;
	}

	struct json_tokener *tok = json_tokener_new();
	struct json_object *obj = tok ? json_tokener_parse_ex(tok, body.data, (int)body.len) : NULL;
	if (tok) json_tokener_free(tok);
	equi_buf_free(&body);

	if (!obj) return -1;

	struct json_object *id_obj = NULL;
	int got = json_object_object_get_ex(obj, "id", &id_obj) && id_obj && json_object_is_type(id_obj, json_type_string);
	if (got) {
		const char *id = json_object_get_string(id_obj);
		size_t len = strlen(id);
		if (len >= cap) len = cap - 1;
		memcpy(out_id, id, len);
		out_id[len] = '\0';
	}
	json_object_put(obj);
	if (got) {
		counter_discord_inc(DISCORD_OK);
		breaker_record_success();
		return 0;
	}
	counter_discord_inc(DISCORD_ERROR);
	breaker_record_failure();
	return -1;
}
