# Profanity-сервис

Путь: `profanity-service`  
Стек: Python (BadWords / `ProfanityFilter`)  
Порт: **9191**

Sidecar проверки текста на запрещённые выражения.  
Его используют и **auth-server**, и **http-server**.

## Что делает

1. Принимает JSON с `text` и/или `texts: []`
2. Прогоняет фильтр с языками и порогом совпадения
3. Возвращает `{ "profane": true|false }`

По умолчанию (compose):

- `PROFANITY_LANGUAGES=en,ru,de,sp`
- `PROFANITY_MATCH_THRESHOLD=0.95`
- Макс. тело: **64 KB**

## HTTP API

| Метод | Путь | Назначение |
|-------|------|------------|
| `GET` | `/health` | Ready после инициализации фильтра |
| `POST` | `/moderate` | JSON-модерация |

Пример:

```bash
curl -s http://localhost:9191/health
curl -s -H 'Content-Type: application/json' \
  -d '{"texts":["привет","какой-то текст"]}' \
  http://localhost:9191/moderate
```

Ответ:

```json
{ "profane": false }
```

Пустые строки → `{ "profane": false }`.

## Как используют серверы

### Клиент (C)

- Auth: `http-service/auth-server/src/clients/profanity.c`
- HTTP: `http-service/http-server/src/clients/profanity.c`

Env: `PROFANITY_URL=http://profanity:9191`

Хелперы:

- `profanity_check_from_env(texts, count, &error)`
- `profanity_reject_from_env(req, res, reason, texts, count)`
  - rejected → `400` + `error.text-profanity`
  - недоступен → `503` + `error.text-moderation-unavailable`

### Где вызывается

| Сервер | Место | Поля |
|--------|-------|------|
| auth-server | создание аккаунта | `name`, `username` |
| http-server | обновление профиля | `username`, `name`, `overview` |
| http-server | папки чатов | `name` группы |

## Локальная разработка

Сервис compose: `profanity`

```bash
docker compose -f docker-compose.dev.yml up profanity
```

Без Docker: `PORT=9191` и `python server.py` в `profanity-service/`.

## Режим пропуска в DEV

Если `PROFANITY_URL` пустой и `TYPE=DEV` / `GO_ENV=DEV`, клиенты пропускают модерацию с warning.  
В проде пустой URL = ошибка недоступности.

## Настройка

| Параметр | Эффект |
|----------|--------|
| `PROFANITY_LANGUAGES` | Какие словари грузить |
| `PROFANITY_MATCH_THRESHOLD` | Чувствительность (выше = строже) |
| timeout в C-клиенте | По умолчанию 5 с |

## Частые ошибки

1. Auth/http смотрят на старые порты после смены на `9191`.
2. Ждать multipart как у NSFW — у profanity только JSON.
3. Выкатить прод с пустым `PROFANITY_URL` — регистрация/профиль начнут отдавать 503.
