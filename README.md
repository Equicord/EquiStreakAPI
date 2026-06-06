# equistreakapi

the backend that powers [equicord streaks](https://equicord.org), the plugin that tracks how many days in a row you've dm'd a friend.

you don't need to run this yourself. the plugin already talks to the official deployment.

## what it does

when you and a friend exchange discord dms two days in a row, your streak goes up by one. this server is what counts the days, remembers the streaks, and serves them back to the plugin.

## see it live

the live landing page shows total streaks and today's active user count at the official deployment.

## run your own

if you want a private deployment, the whole thing is one command:

```sh
docker compose up --build
```

that brings up the api plus a redis instance on `http://localhost:3000`.

for real discord logins to work, fill in `.env` first:

```sh
cp .env.example .env
# edit DISCORD_CLIENT_ID, DISCORD_CLIENT_SECRET, MASTER_API_KEY
docker compose up --build
```

stop with `ctrl c`. it drains in 5 seconds, then exits.

## endpoints you might visit in a browser

- `/` live status: streaks, users today, uptime
- `/health` ok or not ok
- `/metrics` basic json counts
- `/version` what version is running

the full plugin facing api (`/api/streaks/*`, `/api/authorize`) is documented in the source.

## license

agpl 3.0 or later.
