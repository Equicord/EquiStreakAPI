// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#include "args.h"

#include "config.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef EQUISTREAKAPI_VERSION
#define EQUISTREAKAPI_VERSION "0.1.0"
#endif
#ifndef EQUISTREAKAPI_GIT_SHA
#define EQUISTREAKAPI_GIT_SHA "unknown"
#endif
#ifndef EQUISTREAKAPI_BUILD_TIME
#define EQUISTREAKAPI_BUILD_TIME "unknown"
#endif

void args_print_help(void) {
	fputs(
		"Usage: equistreakapi [options]\n"
		"\n"
		"Server:\n"
		"  --port N              Bind port (default 3000, or PORT env)\n"
		"  --bind ADDR           Bind address (default 0.0.0.0, or BIND env)\n"
		"  --public-dir PATH     Static files directory (default ./public)\n"
		"  --threads N           HTTP thread pool size (default nproc)\n"
		"\n"
		"Logging:\n"
		"  --silent              Suppress info logs (also disables access log)\n"
		"  --debug               Enable debug logs (or EQUISTREAKAPI_DEBUG=1)\n"
		"\n"
		"Other:\n"
		"  -h, --help            Show this help and exit\n"
		"  -V, --version         Show version and exit\n"
		"  --print-config        Print the loaded config (redacted) and exit\n"
		"\n"
		"Required environment variables:\n"
		"  DISCORD_CLIENT_ID, DISCORD_CLIENT_SECRET, MASTER_API_KEY (>=32 chars)\n"
		"\n"
		"Optional environment variables:\n"
		"  REDIS_URL (default redis://localhost:6379)\n"
		"  DISCORD_REDIRECT_URI (default http://localhost:<port>/api/authorize)\n"
		"  RATE_LIMIT_PER_MIN (default 60)\n"
		"  AUTH_CACHE_TTL_S (default 300)\n"
		"  THREAD_POOL_SIZE (default min(nproc, 32))\n"
		"  LOG_FORMAT (text|json, default text)\n"
		"  TRUST_XFF (default 0; set to 1 behind a trusted reverse proxy)\n"
		"  EQUISTREAKAPI_DEBUG (1 enables debug logs, same as --debug)\n"
		"  NO_COLOR (any value disables ANSI colors in text-mode logs)\n",
		stdout);
}

void args_print_version(void) {
	puts("equistreakapi " EQUISTREAKAPI_VERSION " (git=" EQUISTREAKAPI_GIT_SHA ", built=" EQUISTREAKAPI_BUILD_TIME ")");
	puts("Copyright (C) 2026 creations. AGPL-3.0-or-later.");
}

static int need_value(const char *flag, int i, int argc) {
	if (i + 1 >= argc) {
		log_error("%s requires a value", flag);
		return -1;
	}
	return 0;
}

int args_parse(int argc, char **argv, struct args *out, struct config *cfg) {
	memset(out, 0, sizeof *out);

	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
		if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
			out->show_help = true;
		} else if (!strcmp(a, "-V") || !strcmp(a, "-v") || !strcmp(a, "--version")) {
			out->show_version = true;
		} else if (!strcmp(a, "--silent")) {
			out->silent = true;
		} else if (!strcmp(a, "--debug")) {
			out->debug = true;
		} else if (!strcmp(a, "--print-config")) {
			out->print_config = true;
		} else if (!strcmp(a, "--port")) {
			if (need_value(a, i, argc) != 0) return -1;
			const char *val = argv[++i];
			char *end = NULL;
			long n = strtol(val, &end, 10);
			if (!end || *end || n < 1 || n > 65535) {
				log_error("--port: invalid value '%s' (expected 1-65535)", val);
				return -1;
			}
			cfg->port = (uint16_t)n;
		} else if (!strcmp(a, "--bind")) {
			if (need_value(a, i, argc) != 0) return -1;
			free(cfg->bind);
			cfg->bind = strdup(argv[++i]);
		} else if (!strcmp(a, "--public-dir")) {
			if (need_value(a, i, argc) != 0) return -1;
			free(cfg->public_dir);
			cfg->public_dir = strdup(argv[++i]);
		} else if (!strcmp(a, "--threads")) {
			if (need_value(a, i, argc) != 0) return -1;
			const char *val = argv[++i];
			char *end = NULL;
			long n = strtol(val, &end, 10);
			if (!end || *end || n < 1 || n > 256) {
				log_error("--threads: invalid value '%s' (expected 1-256)", val);
				return -1;
			}
			cfg->thread_pool_size = (int)n;
		} else {
			log_error("unknown option: %s (try --help)", a);
			return -1;
		}
	}
	return 0;
}
