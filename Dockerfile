FROM node:lts-alpine AS builder
WORKDIR /app

ENV PNPM_HOME="/pnpm"
ENV PATH="$PNPM_HOME:$PATH"
RUN corepack enable

COPY package*.json pnpm-*.yaml ./
RUN pnpm install --frozen-lockfile

COPY . .

RUN pnpm run build

FROM node:lts-alpine
WORKDIR /app

ENV PNPM_HOME="/pnpm"
ENV PATH="$PNPM_HOME:$PATH"
RUN corepack enable

COPY package*.json pnpm-*.yaml ./
RUN pnpm install --prod --frozen-lockfile

COPY --from=builder /app/dist ./dist
COPY --from=builder /app/public ./public

ENV NODE_ENV=production
ENV PORT=3000

EXPOSE 3000
CMD ["pnpm", "start"]
