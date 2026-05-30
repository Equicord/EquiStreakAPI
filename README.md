# equistreakapi

The blazing fast backend powering Equicord Streaks, in C.

A port of [EquiStreakAPI](https://github.com/Equicord/EquiStreakAPI) (TypeScript/Express) to pure C, using libmicrohttpd, hiredis, libcurl, and json-c.

## Run

Fastest path — boots the app plus a Redis sidecar:

```sh
docker compose up --build
# → http://localhost:3000
```

Compose boots with placeholder Discord credentials so `/health`, `/metrics`, and the landing page work immediately. OAuth endpoints (`/api/authorize`, `/api/streaks/*` with `Authorization: Bearer …`) require real Discord OAuth values — set them in a `.env` file or via shell env before `docker compose up`. The admin endpoint (`x-api-key`) works against the placeholder key (`local_test_master_key_at_least_32_chars`) for local testing.

Stop with `Ctrl-C` or `docker compose down`. SIGTERM triggers a 5-second drain (health endpoints flip to 503 immediately so a load balancer can take the pod out of rotation).

Native binary, with your own Redis:

```sh
cp .env.example .env
# edit .env
set -a; . ./.env; set +a
./build/equistreakapi
```

Standalone Docker image:

```sh
docker build -t equistreakapi .
docker run --rm -p 3000:3000 --env-file .env equistreakapi
```

Logs go to stderr (text by default; `LOG_FORMAT=json` for structured one-line JSON).

A Kubernetes example manifest is in `deploy/k8s.yaml.example`.

## Build

```sh
make
```

Dependencies (pkg-config): `libmicrohttpd`, `hiredis`, `libcurl`, `json-c`.

Install commands by distro:

```sh
# Ubuntu / Debian
sudo apt install build-essential pkg-config clang-format \
  libmicrohttpd-dev libhiredis-dev libcurl4-openssl-dev libjson-c-dev

# Alpine
sudo apk add build-base pkgconf libmicrohttpd-dev hiredis-dev curl-dev json-c-dev

# Fedora
sudo dnf install gcc make pkgconf-pkg-config \
  libmicrohttpd-devel hiredis-devel libcurl-devel json-c-devel
```

Run `make help` for the full target list. Other targets include `make sanitize` (ASAN+UBSAN build into `build-san/`), `make fmt` / `make fmt-check`, `make test` (SPDX-header lint), `make install`, `make clean`.

Docker images report `git_sha=unknown` because the `.git` directory isn't copied into the build context; pass it explicitly with `docker build --build-arg GIT_SHA=$(git rev-parse --short HEAD)` if you want the real SHA baked in (requires a small Dockerfile edit).

## HTTP endpoints

| Method | Path                              | Auth          | Notes |
|--------|-----------------------------------|---------------|-------|
| GET    | `/api/authorize?code=…`           | none (rate-limited per IP) | Exchanges a Discord OAuth code; returns Discord's JSON verbatim |
| GET    | `/api/streaks`                    | Bearer        | List of the caller's streaks |
| GET    | `/api/streaks/:recipient_id`      | Bearer        | One streak; 404 if not found |
| POST   | `/api/streaks/:recipient_id`      | Bearer        | Tick the caller's side of the streak |
| POST   | `/api/streaks/migrate`            | Bearer        | Bulk import legacy streaks (capped at 1000 recipients) |
| POST   | `/api/streaks/admin/update`       | `x-api-key`   | Force-write a streak (admin) |
| GET    | `/health`                         | none          | Alias for `/health/ready` |
| GET    | `/health/live`                    | none          | 200 unless draining (use for k8s liveness) |
| GET    | `/health/ready`                   | none          | 200 if not draining and Redis is reachable (use for k8s readiness) |
| GET    | `/metrics`                        | none          | App-level metrics (streak counts, user totals) — JSON |
| GET    | `/metrics/prometheus`             | none          | Service metrics (HTTP/Discord/Redis counters) — Prometheus exposition |
| GET    | `/version`                        | none          | `{version, git_sha, build_time, schema_version, uptime_seconds}` |

`OPTIONS` on any path returns 204 with permissive CORS headers. Wrong-method on a known path returns 405 with an `Allow` header. Every response carries an `X-Request-Id` header for log correlation.

Admin write example:

```sh
curl -X POST \
  -H "x-api-key: $MASTER_API_KEY" \
  -H "Content-Type: application/json" \
  -d '{"user_a_id":"100000000000000001","user_b_id":"200000000000000002","count":5}' \
  http://localhost:3000/api/streaks/admin/update
```

## Configuration

| Env var                  | Required | Default                  | Notes |
|--------------------------|----------|--------------------------|-------|
| `DISCORD_CLIENT_ID`      | yes      | —                        | Discord OAuth app |
| `DISCORD_CLIENT_SECRET`  | yes      | —                        | |
| `MASTER_API_KEY`         | yes      | —                        | Min 32 chars; admin endpoint |
| `REDIS_URL`              | no       | `redis://localhost:6379` | `redis://[user:pass@]host[:port][/db]` (no TLS) |
| `DISCORD_REDIRECT_URI`   | no       | `http://localhost:<port>/api/authorize` | |
| `PORT`                   | no       | `3000`                   | |
| `BIND`                   | no       | `0.0.0.0`                | IPv4 only |
| `THREAD_POOL_SIZE`       | no       | `min(nproc, 32)`         | MHD worker threads |
| `RATE_LIMIT_PER_MIN`     | no       | `60`                     | Per-user; per-IP cap is 6× this |
| `AUTH_CACHE_TTL_S`       | no       | `300`                    | Discord-resolved user-id cache |
| `LOG_FORMAT`             | no       | `text`                   | Set to `json` for one-line JSON logs |
| `TRUST_XFF`              | no       | `0`                      | Set to `1` only behind a proxy that overwrites `X-Forwarded-For`; otherwise spoofable |
| `EQUISTREAKAPI_DEBUG`    | no       | `0`                      | Enable debug logs (same as `--debug`) |
| `NO_COLOR`               | no       | —                        | Disable ANSI colors in text mode |

CLI flags: `--port`, `--bind`, `--public-dir`, `--threads`, `--silent`, `--debug`, `--print-config`, `-h/--help`, `-V/-v/--version`.

## Debugging

`./build/equistreakapi --print-config` dumps the loaded config (with `MASTER_API_KEY` and the Redis password redacted) and exits 0.

`make sanitize` produces `build-san/equistreakapi` built with `-O0 -g3 -fsanitize=address,undefined`. Run it against a real Redis to catch UB/memory issues that release-mode misses.

CI runs build, sanitize, smoke test, and Docker image build on every PR — see `.github/workflows/ci.yml`.

## Operations

- **Deploy behind a TLS reverse proxy** (nginx/Caddy/k8s Ingress). The service speaks plain HTTP. The Discord OAuth flow ships secrets over this socket — do not expose port 3000 directly.
- **SIGTERM** (and `Ctrl-C`/SIGINT) triggers a 5-second drain: `/health`, `/health/live`, and `/health/ready` flip to 503 immediately, in-flight requests complete, then the daemon stops.
- **Auth cache** keys are HMAC-SHA256(server-derived key, token), so a Redis writer cannot fabricate cache entries.
- **Per-IP rate limit** only matters with `TRUST_XFF=1`; otherwise it keys on the TCP peer.
- **Circuit breaker** on Discord opens after 5 consecutive failures and stays open for 30 seconds.

## Install

```sh
make install                # /usr/local
make install PREFIX=/opt    # custom prefix
```

## License

AGPL-3.0-or-later.
