#include "redis/redis.h"

#include "redis/redis_limits.h"

#include <time.h>
#include <stdlib.h>
#include <hiredis/hiredis.h>

static int email_limit_key(const char* email, char* key, size_t key_len, int* seconds_left)
{
    time_t t = time(NULL);
    struct tm tm = *localtime(&t);

    char date[16];
    snprintf(date, sizeof(date), "%04d%02d%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);

    if (snprintf(key, key_len, "user:%s:requests:%s", email, date) >= (int)key_len)
        return 0;

    if (seconds_left)
        *seconds_left = 24 * 60 * 60 - (tm.tm_hour * 3600 + tm.tm_min * 60 + tm.tm_sec);

    return 1;
}

int redis_check_limit_email(const char* email, int max_per_day)
{
    char key[128];
    if (!email || !email_limit_key(email, key, sizeof(key), NULL))
        return 0;

    redisReply* reply = redis_command("GET %s", key);
    if (!reply)
        return 0;

    int count = 0;
    if (reply->type == REDIS_REPLY_STRING)
        count = atoi(reply->str);

    freeReplyObject(reply);
    return count < max_per_day ? 1 : 0;
}

int redis_increment_limit_email(const char* email)
{
    char key[128];
    int seconds_left = 0;
    if (!email || !email_limit_key(email, key, sizeof(key), &seconds_left))
        return 0;

    redisReply* reply = redis_command("INCR %s", key);
    if (reply)
        freeReplyObject(reply);
    else
        return 0;

    if (seconds_left < 1)
        seconds_left = 1;

    reply = redis_command("EXPIRE %s %d", key, seconds_left);
    if (reply)
        freeReplyObject(reply);

    return 1;
}
