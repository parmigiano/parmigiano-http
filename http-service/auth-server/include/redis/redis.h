#ifndef REDIS_H
#define REDIS_H

#include <stddef.h>
#include <stdint.h>
#include <hiredis/hiredis.h>

#define REDIS_SESSION_TTL 172800
#define REDIS_VERIFYCODE_TTL 1800
#define REDIS_AUTH_TOKEN_TTL 600

extern redisContext *redis;

/* Initial redis connect */
int redis_conn(void);

/* Disconnect from redis */
void redis_disconnect(void);

/* Thread-safe redisCommand wrapper */
redisReply* redis_command(const char* format, ...);

/* Thread-safe redisCommandArgv wrapper. argvlen may be NULL. */
redisReply* redis_command_argv(int argc, const char** argv, const size_t* argvlen);

#endif