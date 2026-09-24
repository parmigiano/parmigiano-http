# Parmigiano HTTP — Documentation / Документация

Developer guides for the backend services in this repository.

Руководства для разработчиков по backend-сервисам этого репозитория.

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
