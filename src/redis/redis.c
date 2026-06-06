// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#include "redis.h"

#include "../counters.h"
#include "../http/response.h"
#include "../log.h"

#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

struct redis_url {
	char *host;
	int port;
	char *password;
	int db;
};

static struct redis_url g_url;
static pthread_key_t g_ctx_key;
static pthread_once_t g_key_once = PTHREAD_ONCE_INIT;

static void ctx_destructor(void *p) {
	redisContext *c = p;
	if (c) redisFree(c);
}

static void make_key(void) {
	pthread_key_create(&g_ctx_key, ctx_destructor);
}

static void url_clear(struct redis_url *u) {
	free(u->host);
	free(u->password);
	memset(u, 0, sizeof *u);
}

static int parse_url(const char *url, struct redis_url *out) {
	memset(out, 0, sizeof *out);
	out->port = 6379;
	out->db = 0;

	const char *p = url;
	if (strncmp(p, "redis://", 8) == 0)
		p += 8;
	else if (strncmp(p, "rediss://", 9) == 0) {
		log_error("rediss:// (TLS) is not supported in this build");
		return -1;
	}

	const char *at = strchr(p, '@');
	if (at) {
		const char *colon = memchr(p, ':', (size_t)(at - p));
		const char *pass_start = colon ? colon + 1 : p;
		size_t pass_len = (size_t)(at - pass_start);
		out->password = malloc(pass_len + 1);
		if (!out->password) return -1;
		memcpy(out->password, pass_start, pass_len);
		out->password[pass_len] = '\0';
		p = at + 1;
	}

	const char *slash = strchr(p, '/');
	const char *host_end = slash ? slash : p + strlen(p);
	const char *colon = memchr(p, ':', (size_t)(host_end - p));
	const char *host_stop = colon ? colon : host_end;
	size_t host_len = (size_t)(host_stop - p);
	if (host_len == 0) {
		out->host = strdup("localhost");
	} else {
		out->host = malloc(host_len + 1);
		if (out->host) {
			memcpy(out->host, p, host_len);
			out->host[host_len] = '\0';
		}
	}
	if (!out->host) {
		url_clear(out);
		return -1;
	}

	if (colon) {
		long port = strtol(colon + 1, NULL, 10);
		if (port > 0 && port <= 65535) out->port = (int)port;
	}
	if (slash && *(slash + 1)) {
		long db = strtol(slash + 1, NULL, 10);
		if (db >= 0 && db < 1024) out->db = (int)db;
	}
	return 0;
}

static redisContext *connect_one(void) {
	struct timeval connect_tv = {2, 0};
	redisContext *c = redisConnectWithTimeout(g_url.host, g_url.port, connect_tv);
	if (!c || c->err) {
		if (c) {
			log_error("redis connect %s:%d: %s", g_url.host, g_url.port, c->errstr);
			redisFree(c);
		} else {
			log_error("redis connect %s:%d: out of memory", g_url.host, g_url.port);
		}
		return NULL;
	}
	struct timeval op_tv = {2, 0};
	if (redisSetTimeout(c, op_tv) != REDIS_OK) {
		log_warn("redis setTimeout failed: %s", c->errstr);
	}
	if (g_url.password) {
		redisReply *r = redisCommand(c, "AUTH %s", g_url.password);
		if (!r || r->type == REDIS_REPLY_ERROR) {
			log_error("redis AUTH failed: %s", r ? r->str : "no reply");
			if (r) freeReplyObject(r);
			redisFree(c);
			return NULL;
		}
		freeReplyObject(r);
	}
	if (g_url.db != 0) {
		redisReply *r = redisCommand(c, "SELECT %d", g_url.db);
		if (!r || r->type == REDIS_REPLY_ERROR) {
			log_error("redis SELECT %d failed: %s", g_url.db, r ? r->str : "no reply");
			if (r) freeReplyObject(r);
			redisFree(c);
			return NULL;
		}
		freeReplyObject(r);
	}
	return c;
}

int redis_init(const char *url) {
	pthread_once(&g_key_once, make_key);
	if (parse_url(url, &g_url) != 0) return -1;
	redisContext *probe = connect_one();
	if (!probe) return -1;
	redisReply *r = redisCommand(probe, "PING");
	if (!r || r->type != REDIS_REPLY_STATUS || strcmp(r->str, "PONG") != 0) {
		log_error("redis PING did not return PONG");
		if (r) freeReplyObject(r);
		redisFree(probe);
		return -1;
	}
	freeReplyObject(r);
	redisFree(probe);
	return 0;
}

void redis_shutdown(void) {
	url_clear(&g_url);
}

redisContext *redis_ctx(void) {
	pthread_once(&g_key_once, make_key);
	redisContext *c = pthread_getspecific(g_ctx_key);
	if (c && c->err == 0) return c;
	bool was_broken = (c != NULL);
	if (c) redisFree(c);
	if (was_broken) log_warn("redis: thread reconnecting to %s:%d", g_url.host, g_url.port);
	c = connect_one();
	if (!c) {
		pthread_setspecific(g_ctx_key, NULL);
		return NULL;
	}
	if (was_broken) log_info("redis: thread reconnected to %s:%d", g_url.host, g_url.port);
	pthread_setspecific(g_ctx_key, c);
	return c;
}

void redis_invalidate_ctx(void) {
	counter_redis_inc(REDIS_OP_ERR);
	redisContext *c = pthread_getspecific(g_ctx_key);
	if (c) {
		redisFree(c);
		pthread_setspecific(g_ctx_key, NULL);
	}
}

bool redis_reply_is_error(redisReply *r) {
	return !r || r->type == REDIS_REPLY_ERROR;
}

char *redis_hget_str(const char *key, const char *field) {
	redisContext *c = redis_ctx();
	if (!c) return NULL;
	redisReply *r = redisCommand(c, "HGET %s %s", key, field);
	if (!r) {
		redis_invalidate_ctx();
		return NULL;
	}
	char *out = NULL;
	if (r->type == REDIS_REPLY_STRING) {
		out = malloc((size_t)r->len + 1);
		if (out) {
			memcpy(out, r->str, (size_t)r->len);
			out[r->len] = '\0';
		}
	}
	freeReplyObject(r);
	return out;
}

bool redis_ping_ok(void) {
	redisContext *c = redis_ctx();
	if (!c) return false;
	redisReply *r = redisCommand(c, "PING");
	bool ok = (r && r->type == REDIS_REPLY_STATUS && strcmp(r->str, "PONG") == 0);
	if (r) freeReplyObject(r);
	if (!ok) redis_invalidate_ctx();
	return ok;
}

redisContext *redis_ctx_or_503(struct MHD_Connection *conn) {
	redisContext *c = redis_ctx();
	if (!c) {
		http_send_error(conn, 503, "Redis unavailable");
		return NULL;
	}
	return c;
}

void redis_url_redact(const char *url, char *out, size_t cap) {
	if (!out || cap == 0) return;
	out[0] = '\0';
	if (!url) return;
	const char *scheme_end = strstr(url, "://");
	if (!scheme_end) {
		snprintf(out, cap, "%s", url);
		return;
	}
	const char *creds_end = strchr(scheme_end + 3, '@');
	if (!creds_end) {
		snprintf(out, cap, "%s", url);
		return;
	}
	int prefix_len = (int)(scheme_end + 3 - url);
	snprintf(out, cap, "%.*s***@%s", prefix_len, url, creds_end + 1);
}
