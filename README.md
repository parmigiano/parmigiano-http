<p align="center">
  <img src="https://github.com/parmigiano/parmigiano-http/blob/prepare-assets/assets/parmigianochat.png?raw=true" alt="ParmigianoChat Logo" width="250" height="250">
</p>

<p align="center">
  <a href="https://github.com/parmigiano/parmigiano-http/actions/workflows/ci.yml">
    <img src="https://github.com/parmigiano/parmigiano-http/actions/workflows/ci.yml/badge.svg" alt="PROD CI">
  </a>
</p>

### ParmigianoChat HTTP Server

**ParmigianoChat** — это высокопроизводительный чат для обмена сообщениями в реальном времени.

`Сервер поддерживает двустороннюю синхронизацию сообщений через **HTTP API**, **TCP-транспорт**,
и реализует архитектуру, готовую для масштабирования до миллионов подключений.`

## Основные возможности

-   Поддержка диалогов "один на один"
-   Синхронизация сообщений в реальном времени
-   Работа через HTTP API и TCP
-   Хранение данных в PostgreSQL с кэшированием через Redis
-   Аутентификация и авторизация через Tokens
-   Поддержка миграций базы данных через Goose
-   Возможность аудита изменений сообщений (редактирование, удаление, прочтение)

## Пакеты

| Пакеты     | Версии    |
| ---------- | :-------- |
| C++        | v20       |
| Golang     | v1.24.9   |
| C#         | latest    |
| Make       | v4.4.1    |
| CMake      | v3        |
| Protobuf   | v3        |
| PostgreSql | v16.0.0   |
| GitHub     | undefined |
