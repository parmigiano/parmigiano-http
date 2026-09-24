# Profanity service

Path: `profanity-service`  
Runtime: Python (BadWords / `ProfanityFilter`)  
Port: **9191**

Sidecar that checks text for prohibited language.  
Used by both **auth-server** and **http-server**.

## What it does

1. Accepts JSON body with `text` and/or `texts: []`
2. Runs filter with configured languages + match threshold
3. Returns `{ "profane": true|false }`

Defaults (compose):

- `PROFANITY_LANGUAGES=en,ru,de,sp`
- `PROFANITY_MATCH_THRESHOLD=0.95`
- Max body: **64 KB**

## HTTP API

| Method | Path | Purpose |
|--------|------|---------|
| `GET` | `/health` | Ready when filter initialized |
| `POST` | `/moderate` | JSON moderation |

Example:

```bash
curl -s http://localhost:9191/health
curl -s -H 'Content-Type: application/json' \
  -d '{"texts":["hello","some text"]}' \
  http://localhost:9191/moderate
```

Response:

```json
{ "profane": false }
```

Empty / blank texts → `{ "profane": false }`.

## How servers use it

### Client (C)

- Auth: `http-service/auth-server/src/clients/profanity.c`
- HTTP: `http-service/http-server/src/clients/profanity.c`

Env: `PROFANITY_URL=http://profanity:9191`

Helpers:

- `profanity_check_from_env(texts, count, &error)`
- `profanity_reject_from_env(req, res, reason, texts, count)`
  - rejected → `400` + `error.text-profanity`
  - unavailable → `503` + `error.text-moderation-unavailable`

### Where it is called

| Server | Place | Fields |
|--------|-------|--------|
| auth-server | account create | `name`, `username` |
| http-server | profile update | `username`, `name`, `overview` |
| http-server | chat folders | group `name` |

## Local development

Compose service: `profanity`

```bash
docker compose -f docker-compose.dev.yml up profanity
```

Without Docker: set `PORT=9191` and run `python server.py` in `profanity-service/`.

## Dev skip mode

If `PROFANITY_URL` is empty and `TYPE=DEV` / `GO_ENV=DEV`, clients skip moderation with a warning.  
In production empty URL fails closed (service unavailable).

## Tuning

| Knob | Effect |
|------|--------|
| `PROFANITY_LANGUAGES` | Which dictionaries load |
| `PROFANITY_MATCH_THRESHOLD` | Sensitivity (higher = stricter match) |
| timeout in C client | Default 5s |

## Common pitfalls

1. Auth and http pointing to different/wrong ports after port changes (`9191`).
2. Expecting NSFW-like multipart API — profanity is JSON only.
3. Shipping to prod with empty `PROFANITY_URL` — registration/profile updates will 503.
