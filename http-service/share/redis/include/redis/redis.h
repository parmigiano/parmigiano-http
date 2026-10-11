#ifndef REDIS_H
#define REDIS_H

#include <stddef.h>
#include <hiredis/hiredis.h>

/* Open the process-wide Redis pool from the environment. */
int redis_conn(void);

/* Close the pool and wait for commands already in flight. */
void redis_disconnect(void);

/* Run one command on a pooled connection. The caller frees the reply. */
redisReply* redis_command(const char* format, ...);

/* Same as redis_command for a binary-safe argument vector. argvlen may be NULL. */
redisReply* redis_command_argv(int argc, const char** argv, const size_t* argvlen);

#endif
