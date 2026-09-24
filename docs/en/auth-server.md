# Auth server

Path: `http-service/auth-server`  
Binary: `server-auth`  
Port: **8081**  
Base URL: `/api/v2`

Auth-server owns **authentication only**: email codes, login/register, sessions, account delete.

## Responsibilities

- Send email confirmation codes
- Verify codes
- Create accounts
- Login / logout
- Delete account
- Text moderation via **profanity** on registration (`name`, `username`)

It does **not** serve users/chats/media APIs and does **not** run SQL migrations.

## API routes

| Method | Path | Auth required | Purpose |
|--------|------|---------------|---------|
| `POST` | `/api/v2/auth/confirm/email` | no | Send verification code to email |
| `POST` | `/api/v2/auth/verify` | no | Validate code; may return session or next step |
| `POST` | `/api/v2/auth/create` | no | Register user (after email flow) |
| `POST` | `/api/v2/auth/login` | no | Login with email (+ password if set) |
| `POST` | `/api/v2/auth/logout` | bearer | Revoke current session |
| `DELETE` | `/api/v2/auth/delete` | bearer | Delete account + cleanup avatar in S3 |

Swagger:

- JSON: `/api/v2/doc.api/swagger/json`
- GUI: `/api/v2/doc.api/swagger/gui`

## Typical flows

### New user

1. `POST /auth/confirm/email` `{ "email": "..." }`
2. User receives code by email
3. `POST /auth/verify` `{ "email": "...", "code": 123456 }`
   - if user does not exist → message like **registration required** (`202`)
4. `POST /auth/create` `{ "name", "username", "email", "password?" }`
   - profanity check on `name` / `username`
   - returns session id

### Existing user with password

1. Confirm email / verify code (same as above), or login path depending on client UX
2. `POST /auth/login` with email + password
3. Response contains session id (bearer token for other services)

### Logout / delete

- Logout: `Authorization: Bearer <session>`
- Delete: same bearer; removes DB user and public avatar object from S3 when possible

## Important internals

| Piece | Location | Notes |
|-------|----------|-------|
| Handlers | `src/handlers/v2/auth_handler.c` | All auth endpoints |
| Routes | `src/routes/routes.c` | Only `/auth` + docs |
| Sessions | Redis + `encryption.c` | Session payload encrypted with `SUPER_SECRET_KEY` / `IV` |
| Email codes | Redis verifycode | TTL-based |
| Rate limit | Redis limits | Email send limits |
| Profanity | `src/clients/profanity.c` | `PROFANITY_URL` |
| Auth middleware | `src/middlewares/authenticated.c` | Used by logout/delete |

## Required env (minimum)

See `.env.example`.

Critical keys:

- DB / Redis
- SMTP (`SMTP_*`)
- `SUPER_SECRET_KEY`, `IV` (**same as http-server**)
- `PROFANITY_URL=http://profanity:9191` (compose)
- `I18N_LOCATE=./src/infra/locale`
- `SERVER_BASE_ADDR=http://localhost:8081/api`
- `TYPE=DEV` or `PROD`

Optional for delete cleanup: S3 public bucket settings.

## Dev without sidecars

If `PROFANITY_URL` is empty and `TYPE=DEV`, text moderation is skipped with a warning.  
In production an empty URL is treated as unavailable.

## Compose

Service name: `auth_server`  
Depends on: healthy `profanity`, started `http_server` (so migrations exist first).

## Common pitfalls

1. Different `SUPER_SECRET_KEY`/`IV` between auth and http → sessions from auth invalid on http.
2. Starting auth before http migrations → DB schema missing.
3. Calling `/api/v2/users/*` on auth port → 404 (those live on http-server).
4. Forgetting nginx `/api/v2/auth/` location in prod → auth traffic hits wrong upstream.
