// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#include "args.h"
#include "auth/cache.h"
#include "auth/ratelimit.h"
#include "config.h"
#include "http/request.h"
#include "http/router.h"
#include "http/server.h"
#include "log.h"
#include "redis/redis.h"
#include "routes/authorize.h"
#include "routes/health.h"
#include "routes/metrics.h"
#include "routes/prometheus.h"
#include "routes/streaks.h"
#include "routes/version.h"

#ifndef EQUISTREAKAPI_VERSION
#define EQUISTREAKAPI_VERSION "0.1.0"
#endif
#ifndef EQUISTREAKAPI_GIT_SHA
#define EQUISTREAKAPI_GIT_SHA "unknown"
#endif
#ifndef EQUISTREAKAPI_BUILD_TIME
#define EQUISTREAKAPI_BUILD_TIME "unknown"
#endif
#include "shutdown.h"
#include "static_files.h"

#include <curl/curl.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static atomic_int g_shutdown;
static atomic_int g_draining;
static int g_wake_fd[2] = {-1, -1};

bool is_draining(void) {
	return atomic_load(&g_draining) != 0;
}

static void on_signal(int sig) {
	(void)sig;
	atomic_store(&g_shutdown, 1);
	if (g_wake_fd[1] >= 0) {
		const char b = 1;
		ssize_t w = write(g_wake_fd[1], &b, 1);
		(void)w;
	}
}

static void install_signal_handlers(void) {
	struct sigaction sa;
	memset(&sa, 0, sizeof sa);
	sa.sa_handler = on_signal;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	signal(SIGHUP, SIG_IGN);
	signal(SIGPIPE, SIG_IGN);
}

static int wake_pipe_init(void) {
	if (pipe(g_wake_fd) != 0) return -1;
	for (int i = 0; i < 2; i++) {
		int fl = fcntl(g_wake_fd[i], F_GETFD);
		if (fl >= 0) fcntl(g_wake_fd[i], F_SETFD, fl | FD_CLOEXEC);
	}
	return 0;
}

static void wait_for_shutdown(void) {
	char buf[64];
	while (atomic_load(&g_shutdown) == 0) {
		ssize_t n = read(g_wake_fd[0], buf, sizeof buf);
		if (n < 0 && errno != EINTR) break;
	}
}

int main(int argc, char **argv) {
	struct config cfg;
	struct args args;
	config_init(&cfg);

	if (args_parse(argc, argv, &args, &cfg) != 0) {
		fputs("Usage: equistreakapi [options]    (run --help for full reference)\n", stderr);
		config_free(&cfg);
		return 2;
	}
	if (args.show_help) {
		args_print_help();
		config_free(&cfg);
		return 0;
	}
	if (args.show_version) {
		args_print_version();
		config_free(&cfg);
		return 0;
	}

	log_init(args.silent, args.debug);

	config_load_env(&cfg);

	if (!cfg.public_dir) {
#ifdef EQUISTREAKAPI_PUBLIC_DIR
		if (access(EQUISTREAKAPI_PUBLIC_DIR "/index.html", R_OK) == 0) {
			cfg.public_dir = strdup(EQUISTREAKAPI_PUBLIC_DIR);
		} else {
			cfg.public_dir = strdup("./public");
		}
#else
		cfg.public_dir = strdup("./public");
#endif
	}

	config_validate_or_die(&cfg);

	if (args.print_config) {
		char redacted[256];
		redis_url_redact(cfg.redis_url, redacted, sizeof redacted);
		const char *lf = getenv("LOG_FORMAT");
		printf("port=%u\nbind=%s\npublic_dir=%s\nredis_url=%s\n"
			   "discord_client_id=%s\ndiscord_redirect_uri=%s\n"
			   "master_api_key=*** (length=%zu)\n"
			   "rate_limit_per_min=%d\nauth_cache_ttl_s=%d\nthread_pool_size=%d\n"
			   "trust_xff=%s\nlog_format=%s\n",
			   (unsigned)cfg.port, cfg.bind, cfg.public_dir, redacted,
			   cfg.discord_client_id, cfg.discord_redirect_uri,
			   strlen(cfg.master_api_key),
			   cfg.rate_limit_per_min, cfg.auth_cache_ttl_s, cfg.thread_pool_size,
			   cfg.trust_xff ? "yes" : "no",
			   (lf && (lf[0] == 'j' || lf[0] == 'J')) ? "json" : "text");
		config_free(&cfg);
		return 0;
	}

	if (curl_global_init(CURL_GLOBAL_DEFAULT) != 0) {
		die("curl_global_init failed");
	}

	if (redis_init(cfg.redis_url) != 0) {
		die("redis_init: failed to connect to %s", cfg.redis_url);
	}

	auth_cache_init(&cfg);
	ratelimit_init(&cfg);
	http_set_trust_xff(cfg.trust_xff);

	if (static_files_init(cfg.public_dir) != 0) {
		log_warn("static files unavailable from %s (errno=%d %s); landing page disabled",
				 cfg.public_dir, errno, strerror(errno));
	}

	struct http_router *router = router_create();
	if (!router) die("router_create failed");

	routes_authorize_register(router, &cfg);
	routes_streaks_register(router, &cfg);
	routes_health_register(router, &cfg);
	routes_metrics_register(router, &cfg);
	routes_prometheus_register(router, &cfg);
	routes_version_register(router, &cfg);

	router_set_fallback(router, static_files_fallback_handler, NULL);

	struct http_server *server = http_server_start(&cfg, router);
	if (!server) die("http_server_start failed on %s:%u", cfg.bind, (unsigned)cfg.port);

	log_info("equistreakapi v%s git=%s built=%s schema=%d pid=%d listening on %s:%u",
			 EQUISTREAKAPI_VERSION, EQUISTREAKAPI_GIT_SHA, EQUISTREAKAPI_BUILD_TIME,
			 EQUISTREAKAPI_SCHEMA_VERSION, (int)getpid(),
			 cfg.bind, (unsigned)cfg.port);
	char redis_redacted[256];
	redis_url_redact(cfg.redis_url, redis_redacted, sizeof redis_redacted);
	const char *lf = getenv("LOG_FORMAT");
	const char *lf_name = (lf && (lf[0] == 'j' || lf[0] == 'J')) ? "json" : "text";
	log_info("config: redis=%s threads=%d rate_limit/min=%d auth_cache_ttl=%ds trust_xff=%s log_format=%s public_dir=%s",
			 redis_redacted, cfg.thread_pool_size, cfg.rate_limit_per_min,
			 cfg.auth_cache_ttl_s, cfg.trust_xff ? "yes" : "no",
			 lf_name, cfg.public_dir);

	if (wake_pipe_init() != 0) die("pipe(): %s", strerror(errno));
	install_signal_handlers();
	wait_for_shutdown();

	log_info("draining (health checks will fail-fast)");
	atomic_store(&g_draining, 1);
	sleep(5);

	log_info("shutting down");
	http_server_stop(server);
	router_destroy(router);
	static_files_shutdown();
	redis_shutdown();
	curl_global_cleanup();
	if (g_wake_fd[0] >= 0) close(g_wake_fd[0]);
	if (g_wake_fd[1] >= 0) close(g_wake_fd[1]);
	config_free(&cfg);
	return 0;
}
