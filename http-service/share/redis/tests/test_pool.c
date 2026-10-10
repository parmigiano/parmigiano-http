#define _POSIX_C_SOURCE 200809L

#include "redis/redis.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define THREADS 16
#define ITERATIONS 100

static const char* test_key = "parmigiano:redis_pool:ci_counter";
static int failures = 0;

static void report_error(const char* label, const char* fmt, va_list args)
{
    fprintf(stderr, "%s: ", label);
    vfprintf(stderr, fmt, args);
    fputc('\n', stderr);
}

void logger_info(const char* fmt, ...)
{
    (void)fmt;
}

void logger_warn(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    report_error("WARN", fmt, args);
    va_end(args);
}

void logger_error(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    report_error("ERROR", fmt, args);
    va_end(args);
}

static void* increment_worker(void* data)
{
    (void)data;
    for (int i = 0; i < ITERATIONS; ++i)
    {
        redisReply* reply = redis_command("INCR %s", test_key);
        if (!reply || reply->type != REDIS_REPLY_INTEGER)
            __sync_fetch_and_add(&failures, 1);
        if (reply)
            freeReplyObject(reply);
    }
    return NULL;
}

static int check_reply(redisReply* reply, int type)
{
    int ok = reply && reply->type == type;
    if (reply)
        freeReplyObject(reply);
    return ok;
}

int main(void)
{
    setenv("REDIS_HOST", "127.0.0.1", 1);
    setenv("REDIS_PORT", "6379", 1);
    unsetenv("REDIS_PASSWORD");
    setenv("REDIS_POOL_MAX_OPEN", "4", 1);
    setenv("REDIS_POOL_MIN_IDLE", "2", 1);
    setenv("REDIS_POOL_ACQUIRE_TIMEOUT_MS", "5000", 1);
    setenv("REDIS_CONNECT_TIMEOUT_MS", "2000", 1);
    setenv("REDIS_COMMAND_TIMEOUT_MS", "2000", 1);

    if (!redis_conn() || !redis_conn())
    {
        fprintf(stderr, "Redis pool startup failed\n");
        return 1;
    }

    if (!check_reply(redis_command("SET %s 0", test_key), REDIS_REPLY_STATUS))
        return 2;

    pthread_t threads[THREADS];
    for (int i = 0; i < THREADS; ++i)
        if (pthread_create(&threads[i], NULL, increment_worker, NULL) != 0)
            return 3;

    for (int i = 0; i < THREADS; ++i)
        pthread_join(threads[i], NULL);

    if (failures != 0)
    {
        fprintf(stderr, "%d Redis commands failed under load\n", failures);
        return 4;
    }

    const char* argv[] = {"GET", test_key};
    size_t argvlen[] = {3, strlen(test_key)};
    redisReply* result = redis_command_argv(2, argv, argvlen);
    int expected = THREADS * ITERATIONS;
    if (!result || result->type != REDIS_REPLY_STRING || atoi(result->str) != expected)
    {
        fprintf(stderr, "Counter mismatch, expected %d\n", expected);
        if (result)
            freeReplyObject(result);
        return 5;
    }
    freeReplyObject(result);

    /* A Redis-level command error must not poison the connection. */
    if (!check_reply(redis_command("DOESNOTEXIST"), REDIS_REPLY_ERROR) ||
        !check_reply(redis_command("PING"), REDIS_REPLY_STATUS))
        return 6;

    if (!check_reply(redis_command("DEL %s", test_key), REDIS_REPLY_INTEGER))
        return 7;

    redis_disconnect();
    puts("Redis pool concurrency tests passed");
    return 0;
}
