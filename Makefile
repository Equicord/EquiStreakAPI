.DEFAULT_GOAL := all

.PHONY: help
help:
	@echo "equistreakapi — targets:"
	@echo ""
	@echo "  Build:"
	@echo "    make                 release build into build/equistreakapi"
	@echo "    make sanitize        ASAN+UBSAN build into build-san/equistreakapi"
	@echo "    make clean           remove build/ and build-san/"
	@echo ""
	@echo "  Quality:"
	@echo "    make lint            full lint: fmt-check + SPDX headers + clang-tidy"
	@echo "    make test            SPDX header lint only (tools/check_headers.c)"
	@echo "    make fmt             format all sources with clang-format"
	@echo "    make fmt-check       fail on unformatted sources"
	@echo "    make apply-headers   add the SPDX header to files missing it"
	@echo ""
	@echo "  Install:"
	@echo "    make install                            PREFIX=/usr/local (default)"
	@echo "    make install PREFIX=/opt                custom prefix"
	@echo ""
	@echo "  Local run:"
	@echo "    docker compose up --build               app + Redis on http://localhost:3000"
	@echo "    ./build/equistreakapi --print-config    show loaded config (redacted), exit"
	@echo "    ./build/equistreakapi --help            full CLI/env reference"

VERSION    := 0.1.0
NAME       := equistreakapi

GIT_SHA    := $(shell git -C $(CURDIR) rev-parse --short HEAD 2>/dev/null || echo unknown)
BUILD_TIME := $(shell date -u +%FT%TZ)

BUILDDIR   := build
PREFIX     ?= /usr/local
DESTDIR    ?=
CC              ?= cc
PKG_CONFIG      ?= pkg-config

WARN := \
	-Wall -Wextra -Wpedantic -Wshadow \
	-Wstrict-prototypes -Wmissing-prototypes \
	-Wvla -Wformat=2 -Wformat-security \
	-Wnull-dereference -Wpointer-arith

HARDEN := \
	-fstack-protector-strong -fstack-clash-protection -fcf-protection=full \
	-fno-plt -fno-common -D_FORTIFY_SOURCE=2

CFLAGS  ?= -O2 -g -flto=auto
CFLAGS  += -std=c17 $(WARN) $(HARDEN) \
           -ffunction-sections -fdata-sections \
           -D_XOPEN_SOURCE=700 \
           -DEQUISTREAKAPI_VERSION=\"$(VERSION)\" \
           -DEQUISTREAKAPI_GIT_SHA=\"$(GIT_SHA)\" \
           -DEQUISTREAKAPI_BUILD_TIME=\"$(BUILD_TIME)\" \
           -DEQUISTREAKAPI_PUBLIC_DIR=\"$(PREFIX)/share/$(NAME)/public\" \
           -Isrc \
           -MMD -MP
LDFLAGS ?= -flto=auto -Wl,--gc-sections
LDLIBS  ?=

PKGS_CORE := libmicrohttpd hiredis libcurl json-c

CFLAGS    += $(shell $(PKG_CONFIG) --cflags $(PKGS_CORE)) -pthread
LDLIBS    += $(shell $(PKG_CONFIG) --libs   $(PKGS_CORE)) -lm -pthread

EQUI_SRCS := \
	src/main.c \
	src/args.c \
	src/log.c \
	src/util.c \
	src/util_json.c \
	src/util_time.c \
	src/config.c \
	src/counters.c \
	src/uid_set.c \
	src/static_files.c \
	src/http/server.c \
	src/http/router.c \
	src/http/request.c \
	src/http/response.c \
	src/redis/redis.c \
	src/redis/pipeline.c \
	src/redis/streak_update.c \
	src/auth/discord.c \
	src/auth/oauth.c \
	src/auth/http_client.c \
	src/auth/cache.c \
	src/auth/ratelimit.c \
	src/auth/admin.c \
	src/auth/circuit_breaker.c \
	src/routes/authorize.c \
	src/routes/streaks.c \
	src/routes/streaks_admin.c \
	src/routes/streaks_migrate.c \
	src/routes/health.c \
	src/routes/metrics.c \
	src/routes/prometheus.c \
	src/routes/version.c

EQUI_VENDOR_SRCS := \
	src/vendor/sha256/sha256.c

EQUI_OBJS := $(EQUI_SRCS:%.c=$(BUILDDIR)/%.o) \
             $(EQUI_VENDOR_SRCS:%.c=$(BUILDDIR)/%.o)
EQUI_BIN  := $(BUILDDIR)/$(NAME)

CHECK_SRCS  := tools/check_headers.c
CHECK_OBJS  := $(CHECK_SRCS:%.c=$(BUILDDIR)/%.o)
CHECK_BIN   := $(BUILDDIR)/check_headers

OBJS := $(EQUI_OBJS) $(CHECK_OBJS)
DEPS := $(OBJS:.o=.d)

.PHONY: all
all: $(EQUI_BIN)

$(EQUI_BIN): $(EQUI_OBJS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(CHECK_BIN): $(CHECK_OBJS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

$(BUILDDIR)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILDDIR)/src/vendor/%.o: src/vendor/%.c
	@mkdir -p $(@D)
	$(CC) $(filter-out -Wpedantic -Wmissing-prototypes -Wstrict-prototypes -Wshadow -Wnull-dereference,$(CFLAGS)) -c -o $@ $<

.PHONY: test
test: $(CHECK_BIN)
	$(CHECK_BIN) --check src

.PHONY: apply-headers
apply-headers: $(CHECK_BIN)
	$(CHECK_BIN) --apply src

FMT_SRCS := $(shell find src -path src/vendor -prune -o \( -name '*.c' -o -name '*.h' \) -print) tools/check_headers.c

.PHONY: fmt
fmt:
	clang-format -i $(FMT_SRCS)

.PHONY: fmt-check
fmt-check:
	clang-format --dry-run -Werror $(FMT_SRCS)

CLANG_TIDY ?= clang-tidy

.PHONY: lint
lint: fmt-check test
	@command -v $(CLANG_TIDY) >/dev/null || { echo "$(CLANG_TIDY) not found; install clang-tidy"; exit 1; }
	$(CLANG_TIDY) --quiet --warnings-as-errors='*' $(EQUI_SRCS) -- \
		$(filter-out -MMD -MP -flto=auto -fcf-protection=full -fstack-clash-protection,$(CFLAGS))

SAN_BUILDDIR := build-san
SAN_OBJS     := $(EQUI_SRCS:%.c=$(SAN_BUILDDIR)/%.o)
SAN_VOBJS    := $(EQUI_VENDOR_SRCS:%.c=$(SAN_BUILDDIR)/%.o)
SAN_DEPS     := $(SAN_OBJS:.o=.d)
SAN_BIN      := $(SAN_BUILDDIR)/$(NAME)
SAN_FLAGS    := -O0 -g3 -fsanitize=address,undefined -fno-omit-frame-pointer
SAN_CFLAGS    = $(filter-out -D_FORTIFY_SOURCE=2,$(CFLAGS))

$(SAN_BIN): $(SAN_OBJS) $(SAN_VOBJS)
	@mkdir -p $(@D)
	$(CC) $(SAN_CFLAGS) $(SAN_FLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(SAN_BUILDDIR)/src/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(SAN_CFLAGS) $(SAN_FLAGS) -c -o $@ $<

$(SAN_BUILDDIR)/src/vendor/%.o: src/vendor/%.c
	@mkdir -p $(@D)
	$(CC) $(filter-out -Wpedantic -Wmissing-prototypes -Wstrict-prototypes -Wshadow -Wnull-dereference,$(SAN_CFLAGS)) $(SAN_FLAGS) -c -o $@ $<

.PHONY: sanitize
sanitize: $(SAN_BIN)

-include $(SAN_DEPS)

.PHONY: install
install: $(EQUI_BIN)
	install -Dm755 $(EQUI_BIN) $(DESTDIR)$(PREFIX)/bin/$(NAME)
	install -Dm644 public/index.html $(DESTDIR)$(PREFIX)/share/$(NAME)/public/index.html
	install -Dm644 public/favicon.ico $(DESTDIR)$(PREFIX)/share/$(NAME)/public/favicon.ico
	install -Dm644 public/favicon.webp $(DESTDIR)$(PREFIX)/share/$(NAME)/public/favicon.webp

.PHONY: clean
clean:
	rm -rf $(BUILDDIR) $(SAN_BUILDDIR)

-include $(DEPS)
