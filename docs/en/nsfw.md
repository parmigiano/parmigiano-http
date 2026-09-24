# NSFW service

Path: `nsfw-service`  
Runtime: Node.js + TensorFlow.js (nsfwjs) + sharp  
Port: **9090**

Sidecar that classifies images as NSFW or clean.  
Used by **http-server** (avatars, media moderation pipeline). Auth-server does not call it.

## What it does

1. Accepts an uploaded image (`multipart`, field `image`)
2. Decodes / normalizes with **sharp** (resize, first frame of animated)
3. Runs **nsfwjs** model
4. Returns JSON with `nsfw: true|false` (and scores)

Default thresholds (compose / env):

- `NSFW_THRESHOLD_PORN=0.6`
- `NSFW_THRESHOLD_HENTAI=0.6`
- `NSFW_THRESHOLD_SEXY=0.8`

Max upload size inside sidecar: **2 MB**. Larger files must be resized/normalized by the caller (http-server uses ffmpeg for that in the moderation pipeline).

## HTTP API

| Method | Path | Purpose |
|--------|------|---------|
| `GET` | `/health` | Liveness/readiness (`200` when model loaded) |
| `POST` | `/moderate` | Multipart image check |

Example (local):

```bash
curl -s http://localhost:9090/health
curl -s -F "image=@photo.jpg" http://localhost:9090/moderate
```

Typical success response:

```json
{ "nsfw": false, "scores": { "Porn": 0.01, "Hentai": 0.0, "Sexy": 0.05, "Neutral": 0.9, "Drawing": 0.04 } }
```

## How http-server uses it

- Env: `NSFW_URL=http://nsfw:9090` (Docker DNS name `nsfw`)
- Client wrapper: `http-service/http-server/src/clients/nsfw.c`
- High-level helpers:
  - `nsfw_check_file_from_env(path, content_type, &error)`
  - used in avatar upload and moderation media scan

Results are mapped to i18n keys like `nsfw.rejected`, `nsfw.timeout`, etc.

## Local development

Compose service: `nsfw`

```bash
docker compose -f docker-compose.dev.yml up nsfw
```

Healthcheck waits until the model is ready (can take tens of seconds on first start).

Without Docker, set `PORT=9090` and run the Node service from `nsfw-service/`.

## Dev skip mode

If `NSFW_URL` is empty and http-server runs with `TYPE=DEV`, NSFW checks may be skipped.  
In production leave `NSFW_URL` configured and healthy.

## Tuning

| Knob | Effect |
|------|--------|
| thresholds | Stricter/looser rejection |
| `MAX_BYTES` in server | Sidecar hard limit |
| caller ffmpeg pipeline | Video/gif frame sampling before NSFW |

## Common pitfalls

1. Hitting NSFW before it is healthy → connection / 503 style failures in http-server.
2. Sending video/raw huge files directly → prefer http-server moderation pipeline.
3. Changing port without updating compose + `NSFW_URL`.
