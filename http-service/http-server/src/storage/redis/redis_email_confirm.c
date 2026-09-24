#include "redis/redis.h"

#include "redis/redis_email_confirm.h"

#include <stdlib.h>
#include <string.h>
#include <hiredis/hiredis.h>

static char* email_confirmed_key(const char* key)
{
    char* buffer = malloc(strlen(key) + 17);
    sprintf(buffer, "email_confirmed:%s", key);
    return buffer;
}

void redis_mark_email_confirmed(const char* email)
{
    char* key = email_confirmed_key(email);

    redisReply* reply = redis_command("SET %s 1 EX 1800", key);
    freeReplyObject(reply);

    free(key);
}

int redis_is_email_confirmed(const char* email)
{
    char* key = email_confirmed_key(email);

    redisReply* reply = redis_command("GET %s", key);

    int confirmed = 0;
    if (reply && reply->type == REDIS_REPLY_STRING)
    {
        confirmed = 1;
    }

    free(key);

    freeReplyObject(reply);
    return confirmed;
}
