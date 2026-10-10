#ifndef PARMIGIANO_SHARED_REDIS_H
#define PARMIGIANO_SHARED_REDIS_H

#include <stddef.h>
#include <stdint.h>
#include <hiredis/hiredis.h>

#define REDIS_SESSION_TTL 172800
#define REDIS_VERIFYCODE_TTL 1800
#define REDIS_AUTH_TOKEN_TTL 600

/*
 * Initialize a per-process pool. The call fails if Redis cannot be reached
 * on startup; subsequent broken connections are replaced on the next command.
 */
int redis_conn(void);

/* Call only after request and worker threads have stopped. */
void redis_disconnect(void);

/*
 * Thread-safe synchronous operations. The caller owns the returned reply and
 * must call freeReplyObject(). NULL means a Redis/connection/pool error.
 *
 * Commands are never automatically retried after an I/O failure: they could
 * have executed on the server already (notably INCR, EVAL, token consume).
 */
redisReply* redis_command(const char* format, ...);
redisReply* redis_command_argv(int argc, const char** argv, const size_t* argvlen);

#endif
