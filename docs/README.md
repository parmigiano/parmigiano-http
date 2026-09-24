# Parmigiano HTTP — Documentation / Документация

Developer guides for the HTTP backend in this repository.

Руководства по HTTP-backend этого репозитория.

**Do not start the stack from here.** Shared Postgres/Redis, the Docker network, and the full product start live in **[parmigiano-infra](https://github.com/parmigiano/parmigiano-infra)**. This repo only contains HTTP services and compose files that infra calls.

**Не поднимайте стек отсюда.** Общие Postgres/Redis, Docker-сеть и полный запуск продукта живут в **[parmigiano-infra](https://github.com/parmigiano/parmigiano-infra)**. Здесь только HTTP-сервисы и compose, которые вызывает infra.

## English

- [Overview & architecture](en/overview.md)
- [Auth server](en/auth-server.md)
- [HTTP server](en/http-server.md)
- [NSFW moderation](en/nsfw.md)
- [Profanity moderation](en/profanity.md)

## Русский

- [Обзор и архитектура](ru/overview.md)
- [Auth-сервер](ru/auth-server.md)
- [HTTP-сервер](ru/http-server.md)
- [NSFW-модерация](ru/nsfw.md)
- [Profanity-модерация](ru/profanity.md)

## Quick map / Краткая схема

| Service | Path | Port | Role |
|---------|------|------|------|
| `auth-server` | `http-service/auth-server` | `8081` | Login, register, email verify, sessions |
| `http-server` | `http-service/http-server` | `8080` | Users, chats, media, groups, moderation queue |
| `nsfw` | `nsfw-service` | `9090` | Image NSFW classification |
| `profanity` | `profanity-service` | `9191` | Text profanity check |

In production, nginx routes:

- `/api/v2/auth/*` → auth-server `:8081`
- `/api/v2/*` → http-server `:8080`
