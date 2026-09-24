# Auth-сервер

Путь: `http-service/auth-server`  
Бинарник: `server-auth`  
Порт: **8081**  
Базовый URL: `/api/v2`

Auth-server отвечает **только за аутентификацию**: коды на почту, логин/регистрация, сессии, удаление аккаунта.

## Зона ответственности

- Отправка кода подтверждения email
- Проверка кода
- Создание аккаунта
- Логин / логаут
- Удаление аккаунта
- Текстовая модерация через **profanity** при регистрации (`name`, `username`)

Он **не** отдаёт API users/chats/media и **не** запускает SQL-миграции.

## Роуты API

| Метод | Путь | Нужна auth | Назначение |
|-------|------|------------|------------|
| `POST` | `/api/v2/auth/confirm/email` | нет | Отправить код на email |
| `POST` | `/api/v2/auth/verify` | нет | Проверить код; дальше сессия или следующий шаг |
| `POST` | `/api/v2/auth/create` | нет | Зарегистрировать пользователя |
| `POST` | `/api/v2/auth/login` | нет | Войти по email (+ пароль, если задан) |
| `POST` | `/api/v2/auth/logout` | bearer | Отозвать текущую сессию |
| `DELETE` | `/api/v2/auth/delete` | bearer | Удалить аккаунт + аватар в S3 |

Swagger:

- JSON: `/api/v2/doc.api/swagger/json`
- GUI: `/api/v2/doc.api/swagger/gui`

## Типовые сценарии

### Новый пользователь

1. `POST /auth/confirm/email` `{ "email": "..." }`
2. Пользователь получает код на почту
3. `POST /auth/verify` `{ "email": "...", "code": 123456 }`
   - если пользователя ещё нет → сообщение вроде **требуется регистрация** (`202`)
4. `POST /auth/create` `{ "name", "username", "email", "password?" }`
   - profanity проверяет `name` / `username`
   - в ответе session id

### Существующий пользователь с паролем

1. Подтверждение email / verify (или сценарий логина клиента)
2. `POST /auth/login` с email + password
3. В ответе session id (bearer для остальных сервисов)

### Logout / delete

- Logout: заголовок `Authorization: Bearer <session>`
- Delete: тот же bearer; удаляет пользователя в БД и публичный аватар в S3, если возможно

## Важные внутренности

| Часть | Где | Заметка |
|-------|-----|---------|
| Хендлеры | `src/handlers/v2/auth_handler.c` | Все auth-эндпоинты |
| Роуты | `src/routes/routes.c` | Только `/auth` + docs |
| Сессии | Redis + `encryption.c` | Шифрование через `SUPER_SECRET_KEY` / `IV` |
| Коды email | Redis verifycode | С TTL |
| Лимиты | Redis limits | Ограничение отправки писем |
| Profanity | `src/clients/profanity.c` | `PROFANITY_URL` |
| Auth middleware | `src/middlewares/authenticated.c` | Для logout/delete |

## Минимальный env

См. `.env.example`.

Критично:

- DB / Redis
- SMTP (`SMTP_*`)
- `SUPER_SECRET_KEY`, `IV` (**те же, что у http-server**)
- `PROFANITY_URL=http://profanity:9191` (в compose)
- `I18N_LOCATE=./src/infra/locale`
- `SERVER_BASE_ADDR=http://localhost:8081/api`
- `TYPE=DEV` или `PROD`

Для cleanup при delete нужны настройки публичного S3-бакета.

## Dev без sidecar

Если `PROFANITY_URL` пустой и `TYPE=DEV`, текстовая модерация пропускается с warning.  
В проде пустой URL = недоступность сервиса.

## Compose

Имя сервиса: `auth_server`  
Зависит от: healthy `profanity`, started `http_server` (сначала должны пройти миграции).

## Частые ошибки

1. Разные `SUPER_SECRET_KEY`/`IV` у auth и http → сессии с auth не работают на http.
2. Старт auth до миграций http → нет схемы БД.
3. Запросы `/api/v2/users/*` на порт auth → 404 (это http-server).
4. В проде забыли location nginx `/api/v2/auth/` → трафик уходит не туда.
