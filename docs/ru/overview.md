# Обзор и архитектура

Этот репозиторий — HTTP-backend **Parmigiano Chat**.  
Он разделён на несколько процессов: HTTP, Redis, Postgres, RabbitMQ, S3.

## Структура репозитория

```
parmigiano-http/
├── http-service/
│   ├── auth-server/     # API аутентификации (C)
│   └── http-server/     # основной API продукта (C)
├── nsfw-service/        # sidecar модерации картинок (Node.js)
├── profanity-service/   # sidecar модерации текста (Python)
├── docker-compose.dev.yml
├── docker-compose.prod.yml
├── .nginx/nginx.conf
└── docs/
```

## Схема в рантайме

```
Клиент
  │
  ▼
Nginx (prod)
  ├─ /api/v2/auth/* ──────────────► auth-server :8081
  └─ /api/v2/*      ──────────────► http-server :8080 (+ moderation :8181)
                                       │
                                       ├─► nsfw      :9090
                                       ├─► profanity :9191
                                       ├─► Postgres / Redis / S3
                                       ├─► RabbitMQ (асинхронная модерация медиа)
                                       └─► LibreTranslate (перевод в чатах)
```

Auth-server также ходит в **profanity** (имя/username при регистрации) и использует **Postgres + Redis + SMTP + S3** (удаление аватара при удалении аккаунта).

## Порты

| Сервис | Порт | Заметка |
|--------|------|---------|
| http-server | `8080` | Основной API |
| auth-server | `8081` | Auth API |
| http-server moderation | `8181` | Внутренний HTTP модерации (обычно не наружу) |
| nsfw | `9090` | Sidecar |
| profanity | `9191` | Sidecar |
| RabbitMQ | `5672` / UI `15672` | Очередь |
| LibreTranslate | `4512` → `5000` | Перевод |

## Общие зависимости

- **PostgreSQL** — пользователи, чаты, сообщения, модерация. Миграции запускает **только http-server**. Auth-server подключается к той же БД, но миграции **не** гоняет.
- **Redis** — сессии, коды подтверждения почты, лимиты, флаги email-confirm.
- **S3** — аватары / приватные медиа.
- **SMTP** — письма с кодом (auth-server).

## Локальный запуск (Docker Compose)

1. Создайте env-файлы:

```bash
cp http-service/http-server/.env.example http-service/http-server/.env
cp http-service/auth-server/.env.example http-service/auth-server/.env
```

2. Сеть Docker:

```bash
docker network create parmigiano-net
```

3. Поднять стек:

```bash
docker compose -f docker-compose.dev.yml up --build
```

4. Swagger:

- Auth: `http://localhost:8081/api/v2/doc.api/swagger/gui`
- HTTP: `http://localhost:8080/api/v2/doc.api/swagger/gui`

## Заметки по окружению

- `TYPE=DEV` — локальный режим (CORS, geo отключён, пустые NSFW/PROFANITY могут пропускаться).
- `TYPE=PROD` — прод (geo для не-RU, ограниченный CORS).
- `NSFW_URL` / `PROFANITY_URL` — если пусто и `TYPE=DEV`, клиенты модерации пропускают проверки с warning.
- Auth и HTTP **обязаны** иметь одинаковые `SUPER_SECRET_KEY` / `IV`, иначе сессии не расшифруются.

## Куда идти править

| Задача | Место |
|--------|-------|
| Auth-роуты / хендлеры | `http-service/auth-server` |
| Users / chats / media | `http-service/http-server` |
| Пороги картинок | `nsfw-service` + env в compose |
| Языки / порог мата | `profanity-service` + env в compose |
| Публичный роутинг | `.nginx/nginx.conf` |
| CI / образы | `.github/workflows/*` |

## Дальше читать

1. [Auth-сервер](auth-server.md)
2. [HTTP-сервер](http-server.md)
3. [NSFW](nsfw.md)
4. [Profanity](profanity.md)
