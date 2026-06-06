FROM alpine:3.20 AS builder
WORKDIR /src

RUN apk add --no-cache \
    build-base \
    pkgconf \
    libmicrohttpd-dev \
    hiredis-dev \
    curl-dev \
    json-c-dev

COPY Makefile ./
COPY src ./src
COPY tools ./tools

RUN make -j"$(nproc)"

FROM alpine:3.20
WORKDIR /app

RUN apk add --no-cache \
    libmicrohttpd \
    hiredis \
    libcurl \
    json-c \
 && addgroup -S equi && adduser -S equi -G equi

COPY --from=builder /src/build/equistreakapi /usr/local/bin/equistreakapi
COPY public /app/public

ENV PORT=3000
EXPOSE 3000

HEALTHCHECK --interval=10s --timeout=3s --start-period=5s \
  CMD wget -qO- http://127.0.0.1:3000/health/live >/dev/null 2>&1 || exit 1

USER equi
CMD ["/usr/local/bin/equistreakapi", "--public-dir", "/app/public"]
