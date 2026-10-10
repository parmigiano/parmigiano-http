#define _POSIX_C_SOURCE 200809L

#include "redis/redis.h"
#include "logger.h"

#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define REDIS_DEFAULT_MAX_OPEN 8
#define REDIS_DEFAULT_MIN_IDLE 2
#define REDIS_DEFAULT_ACQUIRE_MS 500
#define REDIS_DEFAULT_CONNECT_MS 2000
#define REDIS_DEFAULT_COMMAND_MS 1000

typedef struct
{
    redisContext* context;
    bool busy;
} redis_pool_slot_t;

typedef struct
{
    pthread_mutex_t mutex;
    pthread_cond_t available;
    redis_pool_slot_t* slots;
    char* host;
    char* password;
    int port;
    int max_open;
    int open_count;
    int acquire_timeout_ms;
    int connect_timeout_ms;
    int command_timeout_ms;
    bool initialized;
    bool shutting_down;
} redis_pool_t;

static redis_pool_t pool = {
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .available = PTHREAD_COND_INITIALIZER,
};

static int env_integer(const char* key, int fallback, int min_value, int max_value)
{
    const char* value = getenv(key);
    if (!value || !*value)
        return fallback;

    char* end = NULL;
    errno = 0;
    long parsed = strtol(value, &end, 10);
    if (errno != 0 || !end || *end != '\0' || parsed < min_value || parsed > max_value)
    {
        logger_warn("Redis: invalid %s, using default %d", key, fallback);
        return fallback;
    }

    return (int)parsed;
}

/* This is called with a reserved slot; it never holds the pool mutex. */
static redisContext* connect_context(void)
{
    struct timeval timeout = {
        .tv_sec = pool.connect_timeout_ms / 1000,
        .tv_usec = (pool.connect_timeout_ms % 1000) * 1000,
    };
    redisContext* context = redisConnectWithTimeout(pool.host, pool.port, timeout);
    if (!context)
    {
        logger_error("Redis: connection allocation failed");
        return NULL;
    }

    if (context->err != REDIS_OK)
    {
        logger_error("Redis: connection failed: %s", context->errstr);
        redisFree(context);
        return NULL;
    }

    struct timeval io_timeout = {
        .tv_sec = pool.command_timeout_ms / 1000,
        .tv_usec = (pool.command_timeout_ms % 1000) * 1000,
    };
    if (redisSetTimeout(context, io_timeout) != REDIS_OK)
    {
        logger_error("Redis: failed to configure command timeout");
        redisFree(context);
        return NULL;
    }

    if (pool.password && pool.password[0])
    {
        const char* argv[] = {"AUTH", pool.password};
        size_t argvlen[] = {4, strlen(pool.password)};
        redisReply* reply = redisCommandArgv(context, 2, argv, argvlen);
        bool authenticated = reply && reply->type == REDIS_REPLY_STATUS &&
                             reply->str && strcmp(reply->str, "OK") == 0;
        if (reply)
            freeReplyObject(reply);
        if (!authenticated)
        {
            logger_error("Redis: authentication failed");
            redisFree(context);
            return NULL;
        }
    }

    return context;
}

static void reset_pool_locked(void)
{
    if (pool.slots)
    {
        for (int i = 0; i < pool.max_open; ++i)
            if (pool.slots[i].context)
                redisFree(pool.slots[i].context);
    }
    free(pool.slots);
    free(pool.host);
    free(pool.password);
    pool.slots = NULL;
    pool.host = NULL;
    pool.password = NULL;
    pool.open_count = 0;
    pool.initialized = false;
    pool.shutting_down = false;
}

int redis_conn(void)
{
    pthread_mutex_lock(&pool.mutex);
    if (pool.initialized)
    {
        pthread_mutex_unlock(&pool.mutex);
        return 1;
    }

    const char* host = getenv("REDIS_HOST");
    const char* port_string = getenv("REDIS_PORT");
    if (!host || !*host || !port_string || !*port_string)
    {
        logger_error("Redis: REDIS_HOST or REDIS_PORT is missing");
        pthread_mutex_unlock(&pool.mutex);
        return 0;
    }

    char* end = NULL;
    errno = 0;
    long port = strtol(port_string, &end, 10);
    if (errno || !end || *end != '\0' || port < 1 || port > 65535)
    {
        logger_error("Redis: invalid REDIS_PORT");
        pthread_mutex_unlock(&pool.mutex);
        return 0;
    }

    pool.port = (int)port;
    pool.max_open = env_integer("REDIS_POOL_MAX_OPEN", REDIS_DEFAULT_MAX_OPEN, 1, 128);
    int min_idle = env_integer("REDIS_POOL_MIN_IDLE", REDIS_DEFAULT_MIN_IDLE, 1, 128);
    if (min_idle > pool.max_open)
        min_idle = pool.max_open;
    pool.acquire_timeout_ms = env_integer("REDIS_POOL_ACQUIRE_TIMEOUT_MS", REDIS_DEFAULT_ACQUIRE_MS, 1, 60000);
    pool.connect_timeout_ms = env_integer("REDIS_CONNECT_TIMEOUT_MS", REDIS_DEFAULT_CONNECT_MS, 1, 60000);
    pool.command_timeout_ms = env_integer("REDIS_COMMAND_TIMEOUT_MS", REDIS_DEFAULT_COMMAND_MS, 1, 60000);

    pool.host = strdup(host);
    const char* password = getenv("REDIS_PASSWORD");
    pool.password = strdup(password ? password : "");
    pool.slots = calloc((size_t)pool.max_open, sizeof(*pool.slots));
    if (!pool.host || !pool.password || !pool.slots)
    {
        logger_error("Redis: failed to allocate pool");
        reset_pool_locked();
        pthread_mutex_unlock(&pool.mutex);
        return 0;
    }

    /* Warm up a small number of connections before accepting requests. */
    for (int i = 0; i < min_idle; ++i)
    {
        pool.slots[i].context = connect_context();
        if (!pool.slots[i].context)
        {
            reset_pool_locked();
            pthread_mutex_unlock(&pool.mutex);
            return 0;
        }
        ++pool.open_count;
    }

    pool.initialized = true;
    logger_info("Redis pool initialized (open=%d, max=%d)", pool.open_count, pool.max_open);
    pthread_mutex_unlock(&pool.mutex);
    return 1;
}

static struct timespec acquire_deadline(void)
{
    struct timespec deadline = {0};
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += pool.acquire_timeout_ms / 1000;
    deadline.tv_nsec += (long)(pool.acquire_timeout_ms % 1000) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L)
    {
        ++deadline.tv_sec;
        deadline.tv_nsec -= 1000000000L;
    }
    return deadline;
}

static redisContext* acquire_connection(int* slot_index)
{
    pthread_mutex_lock(&pool.mutex);
    if (!pool.initialized || pool.shutting_down)
    {
        pthread_mutex_unlock(&pool.mutex);
        return NULL;
    }

    struct timespec deadline = acquire_deadline();
    while (pool.initialized && !pool.shutting_down)
    {
        for (int i = 0; i < pool.max_open; ++i)
        {
            if (pool.slots[i].context && !pool.slots[i].busy)
            {
                pool.slots[i].busy = true;
                *slot_index = i;
                redisContext* context = pool.slots[i].context;
                pthread_mutex_unlock(&pool.mutex);
                return context;
            }
        }

        if (pool.open_count < pool.max_open)
        {
            for (int i = 0; i < pool.max_open; ++i)
            {
                if (pool.slots[i].context || pool.slots[i].busy)
                    continue;

                pool.slots[i].busy = true; /* Reserve the slot while connecting. */
                ++pool.open_count;
                pthread_mutex_unlock(&pool.mutex);

                redisContext* context = connect_context();

                pthread_mutex_lock(&pool.mutex);
                if (!context)
                {
                    pool.slots[i].busy = false;
                    --pool.open_count;
                    pthread_cond_broadcast(&pool.available);
                    pthread_mutex_unlock(&pool.mutex);
                    return NULL;
                }

                pool.slots[i].context = context;
                *slot_index = i;
                pthread_mutex_unlock(&pool.mutex);
                return context;
            }
        }

        int wait_result = pthread_cond_timedwait(&pool.available, &pool.mutex, &deadline);
        if (wait_result == ETIMEDOUT)
        {
            logger_warn("Redis: pool acquire timed out");
            pthread_mutex_unlock(&pool.mutex);
            return NULL;
        }
        if (wait_result != 0)
        {
            pthread_mutex_unlock(&pool.mutex);
            return NULL;
        }
    }

    pthread_mutex_unlock(&pool.mutex);
    return NULL;
}

static void release_connection(int slot_index, bool broken)
{
    redisContext* discarded = NULL;

    pthread_mutex_lock(&pool.mutex);
    redis_pool_slot_t* slot = &pool.slots[slot_index];
    if (broken)
    {
        discarded = slot->context;
        slot->context = NULL;
        --pool.open_count;
    }
    slot->busy = false;
    pthread_cond_broadcast(&pool.available);
    pthread_mutex_unlock(&pool.mutex);

    if (discarded)
        redisFree(discarded);
}

redisReply* redis_command(const char* format, ...)
{
    if (!format)
        return NULL;

    int slot_index = -1;
    redisContext* context = acquire_connection(&slot_index);
    if (!context)
        return NULL;

    va_list arguments;
    va_start(arguments, format);
    redisReply* reply = redisvCommand(context, format, arguments);
    va_end(arguments);

    /* Never replay an ambiguous write automatically after an I/O failure. */
    bool broken = !reply || context->err != REDIS_OK;
    if (broken)
        logger_warn("Redis: command I/O failed, discarding connection");
    release_connection(slot_index, broken);
    return reply;
}

redisReply* redis_command_argv(int argc, const char** argv, const size_t* argvlen)
{
    if (argc <= 0 || !argv)
        return NULL;

    int slot_index = -1;
    redisContext* context = acquire_connection(&slot_index);
    if (!context)
        return NULL;

    redisReply* reply = redisCommandArgv(context, argc, argv, argvlen);
    bool broken = !reply || context->err != REDIS_OK;
    if (broken)
        logger_warn("Redis: argv command I/O failed, discarding connection");
    release_connection(slot_index, broken);
    return reply;
}

void redis_disconnect(void)
{
    pthread_mutex_lock(&pool.mutex);
    if (!pool.initialized)
    {
        pthread_mutex_unlock(&pool.mutex);
        return;
    }

    pool.shutting_down = true;
    pthread_cond_broadcast(&pool.available);

    /* Do not free a hiredis context while a worker is using it. */
    bool busy;
    do
    {
        busy = false;
        for (int i = 0; i < pool.max_open; ++i)
        {
            if (pool.slots[i].busy)
            {
                busy = true;
                break;
            }
        }
        if (busy)
            pthread_cond_wait(&pool.available, &pool.mutex);
    } while (busy);

    reset_pool_locked();
    pthread_mutex_unlock(&pool.mutex);
}
