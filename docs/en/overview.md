# Overview & architecture

This repository is the HTTP backend for **Parmigiano Chat**.  
It is split into several processes that talk over HTTP / Redis / Postgres / RabbitMQ / S3.

## How to run (use infra, not this repo)

Start everything from **[parmigiano-infra](https://github.com/parmigiano/parmigiano-infra)**.  
That repo owns Postgres, Redis, the shared Docker network `parmigiano-net`, and orchestrates HTTP + TCP.

Clone the three repos as **siblings**:

```
parmigiano-infra/     # start here
parmigiano-http/      # this repository
parmigiano-tcp/
```

Then:

```bash
cd parmigiano-infra
make up ENV=dev          # infra + HTTP + TCP
# or only HTTP after infra is up:
make up-infra
make up-http ENV=dev
```

Useful commands (always from `parmigiano-infra`):

| Command | Meaning |
|---------|---------|
| `make up ENV=dev` | Create `parmigiano-net`, start Postgres/Redis, then HTTP and TCP |
| `make up-http ENV=dev` | Start this repo’s compose (`docker-compose.dev.yml`) |
| `make logs-http ENV=dev` | Follow HTTP stack logs |
| `make down ENV=dev` | Stop stacks (volumes kept) |
| `make up ENV=prod` | Same flow with `docker-compose.prod.yml` |

Do **not** run `docker compose -f docker-compose.dev.yml up` from `parmigiano-http` as the normal workflow: Postgres/Redis and the network come from infra. Compose files in this repo exist so infra can call them.

Prepare env files **in this repo** before `make up-http`:

```bash
cp http-service/http-server/.env.example http-service/http-server/.env
cp http-service/auth-server/.env.example http-service/auth-server/.env
```

Swagger after start:

- Auth: `http://localhost:8081/api/v2/doc.api/swagger/gui`
- HTTP: `http://localhost:8080/api/v2/doc.api/swagger/gui`

## Repository layout

```
parmigiano-http/
├── http-service/
│   ├── auth-server/     # authentication API (C)
│   └── http-server/     # main product API (C)
├── nsfw-service/        # image moderation sidecar (Node.js)
├── profanity-service/   # text moderation sidecar (Python)
├── docker-compose.dev.yml
├── docker-compose.prod.yml
├── .nginx/nginx.conf
└── docs/
```

## Runtime map

```
Client
  │
  ▼
Nginx (prod)
  ├─ /api/v2/auth/* ──────────────► auth-server :8081
  └─ /api/v2/*      ──────────────► http-server :8080 (+ moderation :8181)
                                       │
                                       ├─► nsfw      :9090
                                       ├─► profanity :9191
                                       ├─► Postgres / Redis / S3
                                       ├─► RabbitMQ (async media moderation)
                                       └─► LibreTranslate (chat translate)
```

Auth-server also calls **profanity** (registration / profile-like text fields) and uses **Postgres + Redis + SMTP + S3** (account delete avatar cleanup).

## Ports

| Service | Host port | Notes |
|---------|-----------|--------|
| http-server | `8080` | Main API |
| auth-server | `8081` | Auth API |
| http-server moderation | `8181` | Internal moderation HTTP (usually not exposed) |
| nsfw | `9090` | Sidecar |
| profanity | `9191` | Sidecar |
| RabbitMQ | `5672` / UI `15672` | Queue |
| LibreTranslate | `4512` → `5000` | Translate |

## Shared dependencies

- **PostgreSQL** — users, chats, messages, moderation tables. Migrations are owned by **http-server only**. Auth-server connects to the same DB but does **not** run migrations.
- **Redis** — sessions, email verify codes, rate limits, email-confirm flags.
- **S3** — avatars / private media.
- **SMTP** — confirmation emails (auth-server).

## Environment notes

- `TYPE=DEV` — local/dev behavior (CORS, geo middleware bypass, empty NSFW/PROFANITY URLs may be skipped).
- `TYPE=PROD` — production hardening (geo block for non-RU, CORS restricted).
- `NSFW_URL` / `PROFANITY_URL` — if empty and `TYPE=DEV`, moderation clients skip checks with a warning.
- Auth and HTTP **must** share the same `SUPER_SECRET_KEY` / `IV` so session tokens encrypt/decrypt consistently.

## Where to change what

| Task | Place |
|------|--------|
| Auth routes / handlers | `http-service/auth-server` |
| Users / chats / media | `http-service/http-server` |
| Image thresholds | `nsfw-service` + compose env |
| Bad-words languages / threshold | `profanity-service` + compose env |
| Public routing | `.nginx/nginx.conf` |
| CI / images | `.github/workflows/*` |
| Start Postgres / Redis / full stack | [parmigiano-infra](https://github.com/parmigiano/parmigiano-infra) |

## Next reading

1. [Auth server](auth-server.md)
2. [HTTP server](http-server.md)
3. [NSFW](nsfw.md)
4. [Profanity](profanity.md)
