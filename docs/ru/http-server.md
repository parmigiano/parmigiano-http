# HTTP-сервер

Путь: `http-service/http-server`  
Бинарник: `server-http`  
Порты: **8080** (API), **8181** (HTTP модерации)  
Базовый URL: `/api/v2`

HTTP-server — основной API продукта: профили, чаты, медиа, папки чатов, асинхронная модерация.

## Зона ответственности

- API профиля пользователя
- Список чатов / настройки / фоны
- Загрузка медиа в чат
- Папки чатов (groups)
- Постановка задач модерации медиа в RabbitMQ
- Запуск **миграций Postgres** при старте
- Вызовы **NSFW** и **profanity** там, где нужно

Auth-эндпоинты вынесены в **auth-server**. Клиенты используют bearer-сессии, выданные auth.

## Группы API

| Префикс | Auth | Назначение |
|---------|------|------------|
| `/api/v2/doc.api/*` | нет | Swagger |
| `/api/v2/users/*` | bearer | Профиль, аватар |
| `/api/v2/chats/*` | bearer | История, настройки, перевод, AI-бот |
| `/api/v2/media/*` | bearer | Загрузка медиа в чат |
| `/api/v2/groups/*` | bearer | Папки чатов |
| `/api/v2/moderation/scan` | bearer | Поставить задачу модерации (сервер `:8181`) |

Swagger GUI: `http://localhost:8080/api/v2/doc.api/swagger/gui`

## Интеграция модерации

### NSFW (картинки)

Используется при загрузке аватаров / скане медиа:

- Клиент: `src/clients/nsfw.c`
- Env: `NSFW_URL=http://nsfw:9090`
- Пайплайн для очереди: `src/infra/moderation/media.c` (скачать из S3 → кадры через ffmpeg при необходимости → NSFW)

### Profanity (текст)

Используется на пользовательских текстовых полях (профиль, имена групп):

- Клиент: `src/clients/profanity.c`
- Env: `PROFANITY_URL=http://profanity:9191`
- Хелпер: `profanity_reject_from_env(...)` сам пишет HTTP-ошибку клиенту

### Асинхронная модерация медиа

1. API ставит задачу → сообщение в RabbitMQ
2. Воркер читает задачу → цель из БД → `moderation_media_scan`
3. При reject → violation, удаление медиа из S3, возможная блокировка пользователя

## Важные внутренности

| Часть | Где |
|-------|-----|
| Старт / серверы | `src/httpx.c` |
| Роуты | `src/routes/routes.c` |
| Миграции | `src/storage/postgres/migrations/` + `migrate.c` |
| RabbitMQ | `src/rabbitmq/*` |
| Auth middleware | `src/middlewares/authenticated.c` (пользователь из Redis-сессии) |

## Минимальный env

См. `.env.example`.

Критично:

- DB / Redis / RabbitMQ / S3
- `NSFW_URL`, `PROFANITY_URL`
- `LIBRETRANSLATE_URL` (перевод)
- `SUPER_SECRET_KEY`, `IV` (**те же, что у auth-server**)
- `I18N_LOCATE=./src/infra/locale`
- `SERVER_BASE_ADDR=http://localhost:8080/api`
- `TYPE=DEV` или `PROD`

## Dev без sidecar

Пустые `NSFW_URL` / `PROFANITY_URL` при `TYPE=DEV` пропускают проверки (warning в лог).  
В проде так делать нельзя.

## Compose

Имя сервиса: `http_server`  
Зависит от: healthy `nsfw`, `profanity`, `rabbitmq`; started `libretranslate`.

Запуск через **[parmigiano-infra](https://github.com/parmigiano/parmigiano-infra)** (`make up-http ENV=dev` или `make up ENV=dev`). Postgres и Redis поднимает infra, не этот репозиторий.

## Частые ошибки

1. Править уже применённые миграции — CI запрещает; добавляйте новый файл.
2. Ждать `/auth` на `:8080` — он на auth-server `:8081`.
3. Разный secret у auth/http → middleware отклоняет сессии.
4. Собирать старый context `http-service/` — нужен `http-service/http-server`.
