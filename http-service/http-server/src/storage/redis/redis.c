#include "redis/redis.h"
#include "logger.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

redisContext* redis = NULL;

static pthread_mutex_t redis_mutex = PTHREAD_MUTEX_INITIALIZER;

redisReply* redis_command(const char* format, ...)
{
    va_list ap;
    va_start(ap, format);

    pthread_mutex_lock(&redis_mutex);
    redisReply* reply = redisvCommand(redis, format, ap);
    pthread_mutex_unlock(&redis_mutex);

    va_end(ap);
    return reply;
}

int redis_conn(void)
{
    const char* host_env = getenv("REDIS_HOST");
    const char* port_env = getenv("REDIS_PORT");
    const char* password_env = getenv("REDIS_PASSWORD");

    if (!host_env || !port_env)
    {
        return 0;
    }

    int port = atoi(port_env);
    if (port <= 0)
        return 0;

    struct timeval timeout = {5, 0};
    redis = redisConnectWithTimeout(host_env, port, timeout);
    if (!redis || redis->err)
    {
        if (redis)
            redisFree(redis);
        
        redis = NULL;
        return 0;
    }

    if (password_env && strlen(password_env) > 0)
    {
        redisReply* r = redis_command("AUTH %s", password_env);
        if (!r || r->type == REDIS_REPLY_ERROR)
        {
            if (r)
                freeReplyObject(r);
            redisFree(redis);
            redis = NULL;
            return 0;
        }

        freeReplyObject(r);
    }

    logger_info("Successfully connected to the redis!");
    return 1;
}

void redis_disconnect(void)
{
    if (redis)
    {
        redisFree(redis);
        redis = NULL;
    }
}
