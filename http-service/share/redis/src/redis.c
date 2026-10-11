#define _GNU_SOURCE

#include "redis/redis.h"

#include "logger.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    redisContext* ctx;
    int64_t created_ms;
    int64_t idle_since_ms;
    int in_use;
} redis_slot_t;

typedef struct {
    char* host;
    char* password;
    int port;
    int max_open;
    int max_idle;
    int min_idle;
    int acquire_timeout_ms;
    int connect_timeout_ms;
    int command_timeout_ms;
    int max_lifetime_sec;
    int max_idle_time_sec;

    pthread_mutex_t mu;
    pthread_cond_t cv;
    redis_slot_t* slots;
    int open_count;
    int in_use;
    int closing;
    int sync_ready;
} redis_pool_t;

static redis_pool_t redis_pool;

static int64_t mono_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);

    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void add_ms(struct timespec* ts, int ms)
{
    ts->tv_sec += ms / 1000;
    ts->tv_nsec += (long)(ms % 1000) * 1000000L;
    if (ts->tv_nsec >= 1000000000L)
    {
        ts->tv_sec += 1;
        ts->tv_nsec -= 1000000000L;
    }
}

static void ms_to_timeval(int ms, struct timeval* tv)
{
    if (ms < 1)
        ms = 1;

    tv->tv_sec = ms / 1000;
    tv->tv_usec = (ms % 1000) * 1000;
}

static char* dup_str(const char* value)
{
    return strdup(value ? value : "");
}

static int env_int(const char* name, int fallback)
{
    const char* value = getenv(name);
    if (!value || !value[0])
        return fallback;

    char* end = NULL;
    long parsed = strtol(value, &end, 10);

    if (end == value || *end || parsed < 0 || parsed > 100000)
        return fallback;

    return (int)parsed;
}

static int parse_port(const char* value)
{
    if (!value || !value[0])
        return 0;

    char* end = NULL;
    long parsed = strtol(value, &end, 10);

    if (end == value || *end || parsed <= 0 || parsed > 65535)
        return 0;

    return (int)parsed;
}

static int clamp_config(void)
{
    if (!redis_pool.host[0] || redis_pool.port <= 0)
    {
        logger_error("redis_conn: REDIS_HOST and REDIS_PORT are required");
        return 0;
    }

    if (redis_pool.max_open < 1 || redis_pool.max_open > 4096)
    {
        logger_error("redis_conn: REDIS_MAX_OPEN must be between 1 and 4096");
        return 0;
    }

    if (redis_pool.max_idle > redis_pool.max_open)
        redis_pool.max_idle = redis_pool.max_open;
    if (redis_pool.min_idle > redis_pool.max_idle)
        redis_pool.min_idle = redis_pool.max_idle;

    return 1;
}

static void redis_pool_reset(void)
{
    free(redis_pool.host);
    free(redis_pool.password);
    free(redis_pool.slots);

    memset(&redis_pool, 0, sizeof(redis_pool));
}

static redisContext* redis_connect_new(void)
{
    struct timeval connect_timeout;
    struct timeval command_timeout;
    int connect_ms = redis_pool.connect_timeout_ms > 0 ? redis_pool.connect_timeout_ms : 5000;
    ms_to_timeval(connect_ms, &connect_timeout);

    redisContext* ctx = redisConnectWithTimeout(redis_pool.host, redis_pool.port, connect_timeout);
    if (!ctx || ctx->err)
    {
        logger_error("redis connect: %s", ctx ? ctx->errstr : "allocation failed");
        if (ctx)
            redisFree(ctx);

        return NULL;
    }

    if (redis_pool.command_timeout_ms > 0)
    {
        ms_to_timeval(redis_pool.command_timeout_ms, &command_timeout);
        if (redisSetTimeout(ctx, command_timeout) != REDIS_OK)
        {
            logger_error("redis connect: failed to set command timeout: %s", ctx->errstr);
            redisFree(ctx);
            return NULL;
        }
    }

    if (redis_pool.password[0])
    {
        redisReply* reply = redisCommand(ctx, "AUTH %s", redis_pool.password);
        int failed = !reply || reply->type == REDIS_REPLY_ERROR || ctx->err;
        if (failed)
            logger_error("redis auth: %s", (reply && reply->str) ? reply->str : ctx->errstr);
        if (reply)
            freeReplyObject(reply);
        if (failed)
        {
            redisFree(ctx);
            return NULL;
        }
    }

    return ctx;
}

static int claim_empty_locked(void)
{
    if (redis_pool.open_count >= redis_pool.max_open)
        return -1;

    for (int i = 0; i < redis_pool.max_open; i++)
    {
        if (redis_pool.slots[i].ctx || redis_pool.slots[i].in_use)
            continue;

        redis_pool.slots[i].in_use = 1;
        redis_pool.open_count++;
        redis_pool.in_use++;

        return i;
    }

    return -1;
}

static void abandon_reserved_locked(int slot)
{
    redis_pool.slots[slot].ctx = NULL;
    redis_pool.slots[slot].in_use = 0;
    redis_pool.open_count--;
    redis_pool.in_use--;

    pthread_cond_broadcast(&redis_pool.cv);
}

static int connection_expired_locked(int slot, int64_t now)
{
    redis_slot_t* item = &redis_pool.slots[slot];
    if (redis_pool.max_lifetime_sec > 0 && now - item->created_ms >= (int64_t)redis_pool.max_lifetime_sec * 1000)
        return 1;
    if (!item->in_use && redis_pool.max_idle_time_sec > 0 && now - item->idle_since_ms >= (int64_t)redis_pool.max_idle_time_sec * 1000)
        return 1;

    return 0;
}

static int take_idle_locked(void)
{
    for (int i = 0; i < redis_pool.max_open; i++)
    {
        if (!redis_pool.slots[i].ctx || redis_pool.slots[i].in_use)
            continue;

        int64_t now = mono_ms();

        int drop = redis_pool.slots[i].ctx->err || connection_expired_locked(i, now);
        if (drop)
        {
            redisContext* ctx = redis_pool.slots[i].ctx;
            redis_pool.slots[i].ctx = NULL;
            redis_pool.open_count--;
            pthread_mutex_unlock(&redis_pool.mu);
            redisFree(ctx);
            pthread_mutex_lock(&redis_pool.mu);
            return -2;
        }

        redis_pool.slots[i].in_use = 1;
        redis_pool.in_use++;

        return i;
    }

    return -1;
}

static int pool_acquire(int* out_slot)
{
    struct timespec deadline;
    clock_gettime(CLOCK_MONOTONIC, &deadline);

    if (redis_pool.acquire_timeout_ms > 0)
        add_ms(&deadline, redis_pool.acquire_timeout_ms);

    pthread_mutex_lock(&redis_pool.mu);

    for (;;)
    {
        if (redis_pool.closing || !redis_pool.sync_ready)
        {
            pthread_mutex_unlock(&redis_pool.mu);
            logger_error("redis_command: redis pool is closed");
            return 0;
        }

        int slot = take_idle_locked();
        if (slot == -2)
            continue;
        if (slot >= 0)
        {
            *out_slot = slot;
            pthread_mutex_unlock(&redis_pool.mu);
            return 1;
        }

        slot = claim_empty_locked();
        if (slot >= 0)
        {
            pthread_mutex_unlock(&redis_pool.mu);
            redisContext* ctx = redis_connect_new();
            pthread_mutex_lock(&redis_pool.mu);

            if (!ctx || redis_pool.closing)
            {
                abandon_reserved_locked(slot);
                pthread_mutex_unlock(&redis_pool.mu);

                if (ctx)
                    redisFree(ctx);

                return 0;
            }

            redis_pool.slots[slot].ctx = ctx;
            redis_pool.slots[slot].created_ms = mono_ms();
            redis_pool.slots[slot].idle_since_ms = redis_pool.slots[slot].created_ms;

            *out_slot = slot;
            pthread_mutex_unlock(&redis_pool.mu);

            return 1;
        }

        if (redis_pool.acquire_timeout_ms <= 0)
        {
            pthread_cond_wait(&redis_pool.cv, &redis_pool.mu);
            continue;
        }

        int wait_result = pthread_cond_timedwait(&redis_pool.cv, &redis_pool.mu, &deadline);
        if (wait_result != 0)
        {
            pthread_mutex_unlock(&redis_pool.mu);
            logger_error("redis_command: timed out waiting for a redis connection");
            return 0;
        }
    }
}

static void pool_release(int slot, int discard)
{
    redisContext* ctx = redis_pool.slots[slot].ctx;
    int bad = !ctx || ctx->err;

    pthread_mutex_lock(&redis_pool.mu);

    int64_t now = mono_ms();
    int expired = ctx && connection_expired_locked(slot, now);
    redis_pool.slots[slot].in_use = 0;
    redis_pool.in_use--;

    int idle_after = redis_pool.open_count - redis_pool.in_use;
    if (discard || bad || expired || redis_pool.closing || idle_after > redis_pool.max_idle)
    {
        redis_pool.slots[slot].ctx = NULL;
        if (ctx)
            redis_pool.open_count--;

        pthread_cond_broadcast(&redis_pool.cv);
        pthread_mutex_unlock(&redis_pool.mu);

        if (ctx)
            redisFree(ctx);

        return;
    }

    redis_pool.slots[slot].idle_since_ms = now;
    pthread_cond_broadcast(&redis_pool.cv);
    pthread_mutex_unlock(&redis_pool.mu);
}

static void pool_fill_idle(void)
{
    for (;;)
    {
        pthread_mutex_lock(&redis_pool.mu);
        int idle = redis_pool.open_count - redis_pool.in_use;
        if (redis_pool.closing || idle >= redis_pool.min_idle || redis_pool.open_count >= redis_pool.max_open)
        {
            pthread_mutex_unlock(&redis_pool.mu);
            return;
        }

        int slot = claim_empty_locked();
        if (slot < 0)
        {
            pthread_mutex_unlock(&redis_pool.mu);
            return;
        }
        pthread_mutex_unlock(&redis_pool.mu);

        redisContext* ctx = redis_connect_new();

        pthread_mutex_lock(&redis_pool.mu);
        if (!ctx || redis_pool.closing)
        {
            abandon_reserved_locked(slot);
            pthread_mutex_unlock(&redis_pool.mu);
            if (ctx)
                redisFree(ctx);
            return;
        }

        redis_pool.slots[slot].ctx = ctx;
        redis_pool.slots[slot].created_ms = mono_ms();
        redis_pool.slots[slot].idle_since_ms = redis_pool.slots[slot].created_ms;
        redis_pool.slots[slot].in_use = 0;
        redis_pool.in_use--;
        pthread_cond_broadcast(&redis_pool.cv);
        pthread_mutex_unlock(&redis_pool.mu);
    }
}

static int command_begin(redisContext** out_ctx, int* out_slot)
{
    if (!redis_pool.sync_ready)
        return 0;

    int slot = -1;
    if (!pool_acquire(&slot))
        return 0;

    *out_slot = slot;
    *out_ctx = redis_pool.slots[slot].ctx;
    return 1;
}

static redisReply* command_end(int slot, redisContext* ctx, redisReply* reply)
{
    pool_release(slot, !reply || !ctx || ctx->err);
    return reply;
}

redisReply* redis_command(const char* format, ...)
{
    redisContext* ctx = NULL;
    int slot = -1;
    if (!command_begin(&ctx, &slot))
        return NULL;

    va_list ap;
    va_start(ap, format);
    redisReply* reply = redisvCommand(ctx, format, ap);
    va_end(ap);
    
    return command_end(slot, ctx, reply);
}

redisReply* redis_command_argv(int argc, const char** argv, const size_t* argvlen)
{
    if (argc <= 0 || !argv)
        return NULL;

    redisContext* ctx = NULL;
    int slot = -1;
    if (!command_begin(&ctx, &slot))
        return NULL;

    redisReply* reply = redisCommandArgv(ctx, argc, argv, argvlen);
    return command_end(slot, ctx, reply);
}

int redis_conn(void)
{
    if (redis_pool.sync_ready)
        return 1;

    const char* host = getenv("REDIS_HOST");
    const char* port_env = getenv("REDIS_PORT");
    const char* password = getenv("REDIS_PASSWORD");
    int port = parse_port(port_env);

    redis_pool.host = dup_str(host);
    redis_pool.password = dup_str(password);
    redis_pool.port = port;
    redis_pool.max_open = env_int("REDIS_MAX_OPEN", 16);
    redis_pool.max_idle = env_int("REDIS_MAX_IDLE", 8);
    redis_pool.min_idle = env_int("REDIS_MIN_IDLE", 2);
    redis_pool.acquire_timeout_ms = env_int("REDIS_ACQUIRE_TIMEOUT_MS", 3000);
    redis_pool.connect_timeout_ms = env_int("REDIS_CONNECT_TIMEOUT_MS", 5000);
    redis_pool.command_timeout_ms = env_int("REDIS_COMMAND_TIMEOUT_MS", 5000);
    redis_pool.max_lifetime_sec = env_int("REDIS_MAX_LIFETIME_SEC", 1800);
    redis_pool.max_idle_time_sec = env_int("REDIS_MAX_IDLE_TIME_SEC", 300);

    if (!redis_pool.host || !redis_pool.password || !clamp_config())
    {
        redis_pool_reset();
        return 0;
    }

    redis_pool.slots = calloc((size_t)redis_pool.max_open, sizeof(redis_slot_t));
    if (!redis_pool.slots)
    {
        redis_pool_reset();
        return 0;
    }

    pthread_mutex_init(&redis_pool.mu, NULL);

    pthread_condattr_t attr;
    pthread_condattr_init(&attr);
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    pthread_cond_init(&redis_pool.cv, &attr);
    pthread_condattr_destroy(&attr);
    redis_pool.sync_ready = 1;

    int warm = redis_pool.min_idle > 0 ? redis_pool.min_idle : 1;
    int saved_min_idle = redis_pool.min_idle;
    redis_pool.min_idle = warm;
    pool_fill_idle();
    redis_pool.min_idle = saved_min_idle;

    if (redis_pool.open_count < warm)
    {
        logger_error("redis_conn: failed to open the minimum idle connections");
        redis_disconnect();
        return 0;
    }

    logger_info("Redis pool opened, idle=%d max_open=%d", redis_pool.open_count, redis_pool.max_open);
    return 1;
}

void redis_disconnect(void)
{
    if (!redis_pool.sync_ready)
    {
        redis_pool_reset();
        return;
    }

    pthread_mutex_lock(&redis_pool.mu);
    redis_pool.closing = 1;
    pthread_cond_broadcast(&redis_pool.cv);
    while (redis_pool.in_use > 0)
        pthread_cond_wait(&redis_pool.cv, &redis_pool.mu);

    for (int i = 0; i < redis_pool.max_open; i++)
    {
        if (!redis_pool.slots[i].ctx)
            continue;
        redisFree(redis_pool.slots[i].ctx);
        redis_pool.slots[i].ctx = NULL;
    }

    redis_pool.open_count = 0;
    pthread_mutex_unlock(&redis_pool.mu);
    pthread_cond_destroy(&redis_pool.cv);
    pthread_mutex_destroy(&redis_pool.mu);
    redis_pool.sync_ready = 0;
    redis_pool_reset();
}
