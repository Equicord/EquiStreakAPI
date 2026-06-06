// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#include "admin.h"

#include "../config.h"
#include "../http/request.h"
#include "../util.h"

bool admin_check(struct http_request *r, const struct config *cfg) {
	if (!cfg || !cfg->master_api_key || !cfg->master_api_key[0]) return false;
	const char *got = http_req_header(r, "x-api-key");
	if (!got) return false;
	return equi_const_time_eq(got, cfg->master_api_key);
}
