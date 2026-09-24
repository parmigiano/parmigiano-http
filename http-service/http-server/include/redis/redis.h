#ifndef REDIS_H
#define REDIS_H

#include <stdint.h>
#include <hiredis/hiredis.h>

#define REDIS_SESSION_TTL 172800
#define REDIS_VERIFYCODE_TTL 1800

extern redisContext *redis;

/* Initial redis connect */
int redis_conn(void);

/* Disconnect from redis */
void redis_disconnect(void);

/* Thread-safe redisCommand wrapper */
redisReply* redis_command(const char* format, ...);

#endif