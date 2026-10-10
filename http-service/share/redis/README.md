# Shared Redis client (hiredis)

Shared by `http-service/http-server` and `http-service/auth-server`.
Each **process** owns its own connection pool. This is a C library, not an
extra Redis instance or HTTP service.

## Configuration

All variables are optional except `REDIS_HOST` and `REDIS_PORT`.

| Environment variable | Default | Meaning |
| --- | ---: | --- |
| `REDIS_POOL_MAX_OPEN` | 8 | Maximum simultaneous connections per process (1–128) |
| `REDIS_POOL_MIN_IDLE` | 2 | Connections pre-opened on startup (clamped to max; 1–128) |
| `REDIS_POOL_ACQUIRE_TIMEOUT_MS` | 500 | Maximum wait for a free slot |
| `REDIS_CONNECT_TIMEOUT_MS` | 2000 | New TCP connection timeout |
| `REDIS_COMMAND_TIMEOUT_MS` | 1000 | Read/write timeout for one synchronous command |
| `REDIS_PASSWORD` | empty | Optional AUTH password (never logged) |

Do not size pools by `http_config.max_clients`; one Redis process can
serve many clients, and each HTTP service has its own pool.

## Usage and failure behavior

`redis_conn()` initializes the pool; startup fails if it cannot connect or
authenticate. `redis_command()` and `redis_command_argv()` borrow a
connection exclusively for a synchronous command. The caller still owns
and frees the resulting `redisReply*` with `freeReplyObject()`.

When an I/O call fails, that connection is discarded. A later command
opens a replacement; **the failed command is not automatically retried**,
because Redis may already have performed a write (`INCR`, `EVAL`,
one-time token consume, etc.). Both command functions return `NULL` if
no connection can be acquired or an I/O error occurred. Redis command
errors are returned as `REDIS_REPLY_ERROR` and keep the connection.

Call `redis_disconnect()` only after stopping all HTTP request and
background worker threads.

## Testing

Run the `redis-pool-test` CI job. It starts a standalone Redis service,
compiles the shared implementation against hiredis, and exercises
multi-threaded commands and the `argv` API. Normal Docker builds pick up
the shared files through the existing `COPY share /share` step.
