# Overview & architecture

This repository is the HTTP backend for **Parmigiano Chat**.  
It is split into several processes that talk over HTTP / Redis / Postgres / RabbitMQ / S3.

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

## Local start (Docker Compose)

1. Create env files:

```bash
cp http-service/http-server/.env.example http-service/http-server/.env
cp http-service/auth-server/.env.example http-service/auth-server/.env
```

2. Ensure Docker network exists:

```bash
docker network create parmigiano-net
```

3. Start stack:

```bash
docker compose -f docker-compose.dev.yml up --build
```

4. Swagger:

- Auth: `http://localhost:8081/api/v2/doc.api/swagger/gui`
- HTTP: `http://localhost:8080/api/v2/doc.api/swagger/gui`

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

## Next reading

1. [Auth server](auth-server.md)
2. [HTTP server](http-server.md)
3. [NSFW](nsfw.md)
4. [Profanity](profanity.md)
