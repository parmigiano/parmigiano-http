# HTTP server

Path: `http-service/http-server`  
Binary: `server-http`  
Ports: **8080** (API), **8181** (moderation HTTP)  
Base URL: `/api/v2`

HTTP-server is the main product API: profiles, chats, media upload, folder groups, async moderation.

## Responsibilities

- Authenticated user/profile APIs
- Chat list / settings / backgrounds
- Media upload into chats
- Chat folders (groups)
- Enqueue media moderation tasks to RabbitMQ
- Run **Postgres migrations** on startup
- Call **NSFW** and **profanity** where needed

Auth endpoints were moved to **auth-server**. Clients still use bearer sessions issued by auth.

## API groups

| Prefix | Auth | Purpose |
|--------|------|---------|
| `/api/v2/doc.api/*` | no | Swagger |
| `/api/v2/users/*` | bearer | Profile, avatar |
| `/api/v2/chats/*` | bearer | History, settings, translate, AI bot |
| `/api/v2/media/*` | bearer | Upload media into chat |
| `/api/v2/groups/*` | bearer | Chat folders |
| `/api/v2/moderation/scan` | bearer | Queue moderation task (on `:8181` server) |

Swagger GUI: `http://localhost:8080/api/v2/doc.api/swagger/gui`

## Moderation integration

### NSFW (images)

Used when uploading avatars / scanning media:

- Client: `src/clients/nsfw.c`
- Env: `NSFW_URL=http://nsfw:9090`
- Pipeline helper for queued media: `src/infra/moderation/media.c` (download from S3 → ffmpeg frames if needed → NSFW)

### Profanity (text)

Used on user-facing text fields (e.g. profile update, group names):

- Client: `src/clients/profanity.c`
- Env: `PROFANITY_URL=http://profanity:9191`
- Helper: `profanity_reject_from_env(...)` returns HTTP error to client

### Async media moderation

1. Client/API calls moderation scan → message published to RabbitMQ
2. Worker consumes task → loads target from DB → `moderation_media_scan`
3. On reject → violation recorded, media may be deleted from S3, user may be blocked

## Important internals

| Piece | Location |
|-------|----------|
| Boot / servers | `src/httpx.c` |
| Routes | `src/routes/routes.c` |
| Migrations | `src/storage/postgres/migrations/` + `migrate.c` |
| RabbitMQ runtime | `src/rabbitmq/*` |
| Auth middleware | `src/middlewares/authenticated.c` (loads user from Redis session) |

## Required env (minimum)

See `.env.example`.

Critical:

- DB / Redis / RabbitMQ / S3
- `NSFW_URL`, `PROFANITY_URL`
- `LIBRETRANSLATE_URL` (translate endpoints)
- `SUPER_SECRET_KEY`, `IV` (**same as auth-server**)
- `I18N_LOCATE=./src/infra/locale`
- `SERVER_BASE_ADDR=http://localhost:8080/api`
- `TYPE=DEV` or `PROD`

## Dev without sidecars

Empty `NSFW_URL` / `PROFANITY_URL` with `TYPE=DEV` skips checks (logged warning).  
Do not rely on this in production.

## Compose

Service name: `http_server`  
Depends on: healthy `nsfw`, `profanity`, `rabbitmq`; started `libretranslate`.

## Common pitfalls

1. Editing migrations already applied in shared DB — CI forbids mutating old files; add a new migration.
2. Expecting `/auth` on `:8080` — moved to auth-server `:8081`.
3. Auth/http secret mismatch → middleware rejects sessions.
4. Building old context `http-service/` — correct context is `http-service/http-server`.
