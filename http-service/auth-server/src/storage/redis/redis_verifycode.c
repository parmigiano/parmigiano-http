#include "redis/redis.h"
#include "redis/redis_verifycode.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char* verifycode_key(const char* key)
{
    char* buffer = malloc(strlen(key) + 12);
    if (!buffer)
        return NULL;

    sprintf(buffer, "verifycode:%s", key);
    return buffer;
}

int redis_verifycode_create(const char* email, int code)
{
    if (!email)
        return 1;

    char* key = verifycode_key(email);
    if (!key)
        return 1;

    char val[64];
    snprintf(val, sizeof(val), "%d", code);

    redisReply* r = redis_command("SETEX %s %d %s", key, REDIS_VERIFYCODE_TTL, val);
    if (!r)
    {
        free(key);
        return 1;
    }

    freeReplyObject(r);
    free(key);

    return 0;
}

int redis_verifycode_delete(const char* email)
{
    if (!email)
        return 1;

    char* key = verifycode_key(email);
    if (!key)
        return 1;

    redisReply* reply = redis_command("DEL %s", key);
    free(key);

    if (!reply || reply->type == REDIS_REPLY_ERROR)
    {
        if (reply)
            freeReplyObject(reply);
        return 1;
    }

    freeReplyObject(reply);
    return 0;
}

bool redis_verifycode_get(const char* email, int* out_code)
{
    if (!email || !out_code)
        return false;

    char* key = verifycode_key(email);
    if (!key)
        return false;

    redisReply* r = redis_command("GET %s", key);
    if (!r)
    {
        free(key);
        return false;
    }

    bool exists = false;
    if (r->type == REDIS_REPLY_STRING)
    {
        *out_code = atoi(r->str);
        exists = true;
    }

    freeReplyObject(r);
    free(key);

    return exists;
}

int redis_verifycode_consume(const char* email, int code, int* out_ttl)
{
    static const char script[] = "local v = redis.call('GET', KEYS[1]) "
                                 "if not v then return {0, 0} end "
                                 "if v ~= ARGV[1] then return {0, 0} end "
                                 "local ttl = redis.call('TTL', KEYS[1]) "
                                 "redis.call('DEL', KEYS[1]) "
                                 "return {1, ttl}";

    if (out_ttl)
        *out_ttl = 0;

    if (!email)
        return -1;

    char* key = verifycode_key(email);
    if (!key)
        return -1;

    char code_buf[32];
    snprintf(code_buf, sizeof(code_buf), "%d", code);

    const char* argv[] = {"EVAL", script, "1", key, code_buf};
    size_t argvlen[] = {4, sizeof(script) - 1, 1, strlen(key), strlen(code_buf)};

    redisReply* reply = redis_command_argv(5, argv, argvlen);
    free(key);

    if (!reply || reply->type != REDIS_REPLY_ARRAY || reply->elements < 2 || !reply->element[0] || reply->element[0]->type != REDIS_REPLY_INTEGER)
    {
        if (reply)
            freeReplyObject(reply);
        return -1;
    }

    long long status = reply->element[0]->integer;
    if (status == 1 && out_ttl && reply->element[1] && reply->element[1]->type == REDIS_REPLY_INTEGER)
        *out_ttl = (int)reply->element[1]->integer;

    freeReplyObject(reply);

    if (status == 1)
        return 1;
    if (status == 0)
        return 0;
    return -1;
}

int redis_verifycode_restore(const char* email, int code, int ttl_seconds)
{
    if (!email || ttl_seconds <= 0)
        return -1;

    char* key = verifycode_key(email);
    if (!key)
        return -1;

    char val[64];
    char ttl_buf[16];
    snprintf(val, sizeof(val), "%d", code);
    snprintf(ttl_buf, sizeof(ttl_buf), "%d", ttl_seconds);

    const char* argv[] = {"SET", key, val, "EX", ttl_buf, "NX"};
    size_t argvlen[] = {3, strlen(key), strlen(val), 2, strlen(ttl_buf), 2};
    redisReply* reply = redis_command_argv(6, argv, argvlen);
    free(key);

    if (!reply || reply->type == REDIS_REPLY_ERROR)
    {
        if (reply)
            freeReplyObject(reply);
        return -1;
    }

    freeReplyObject(reply);
    return 0;
}
