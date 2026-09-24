# NSFW-сервис

Путь: `nsfw-service`  
Стек: Node.js + TensorFlow.js (nsfwjs) + sharp  
Порт: **9090**

Sidecar, который классифицирует изображения как NSFW или чистые.  
Его вызывает **http-server** (аватары, пайплайн модерации медиа). Auth-server к нему не ходит.

## Что делает

1. Принимает картинку (`multipart`, поле `image`)
2. Декодирует / нормализует через **sharp** (resize, первый кадр анимации)
3. Прогоняет модель **nsfwjs**
4. Отдаёт JSON с `nsfw: true|false` (и scores)

Пороги по умолчанию (compose / env):

- `NSFW_THRESHOLD_PORN=0.6`
- `NSFW_THRESHOLD_HENTAI=0.6`
- `NSFW_THRESHOLD_SEXY=0.8`

Лимит размера внутри sidecar: **2 MB**. Большие файлы должен заранее уменьшить вызывающий код (в пайплайне http-server это делает ffmpeg).

## HTTP API

| Метод | Путь | Назначение |
|-------|------|------------|
| `GET` | `/health` | Готовность (`200`, когда модель загружена) |
| `POST` | `/moderate` | Проверка multipart-картинки |

Пример:

```bash
curl -s http://localhost:9090/health
curl -s -F "image=@photo.jpg" http://localhost:9090/moderate
```

Типичный ответ:

```json
{ "nsfw": false, "scores": { "Porn": 0.01, "Hentai": 0.0, "Sexy": 0.05, "Neutral": 0.9, "Drawing": 0.04 } }
```

## Как использует http-server

- Env: `NSFW_URL=http://nsfw:9090` (DNS-имя контейнера `nsfw`)
- Клиент: `http-service/http-server/src/clients/nsfw.c`
- Хелперы:
  - `nsfw_check_file_from_env(path, content_type, &error)`
  - аватар + `moderation_media_scan`

Результаты мапятся в i18n-ключи: `nsfw.rejected`, `nsfw.timeout` и т.д.

## Локальная разработка

Сервис compose: `nsfw` (в HTTP-compose этого репо).  
HTTP-стек поднимайте из **[parmigiano-infra](https://github.com/parmigiano/parmigiano-infra)**:

```bash
cd ../parmigiano-infra
make up-http ENV=dev
```

Healthcheck ждёт готовности модели (первый старт может занять десятки секунд).

Без Docker: `PORT=9090` и запуск Node-сервиса из `nsfw-service/`.

## Режим пропуска в DEV

Если `NSFW_URL` пустой и у http-server `TYPE=DEV`, NSFW-проверки могут пропускаться.  
В проде `NSFW_URL` должен быть задан, сервис — healthy.

## Настройка

| Параметр | Эффект |
|----------|--------|
| thresholds | Жёстче/мягче reject |
| `MAX_BYTES` в server | Жёсткий лимит sidecar |
| ffmpeg у вызывающего | Нарезка кадров video/gif до NSFW |

## Частые ошибки

1. Ходить в NSFW до готовности → connection / 503 у http-server.
2. Кидать огромное видео напрямую в sidecar → лучше пайплайн http-server.
3. Сменить порт и забыть обновить compose + `NSFW_URL`.
