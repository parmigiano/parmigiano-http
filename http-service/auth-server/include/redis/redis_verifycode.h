#ifndef REDIS_VERIFYCODE_H
#define REDIS_VERIFYCODE_H

#include <stdbool.h>

#define REDIS_VERIFYCODE_TTL 1800

int redis_verifycode_create(const char* email, int code);

int redis_verifycode_delete(const char* email);

bool redis_verifycode_get(const char* email, int* out_code);

/* 1 consumed (*out_ttl is the remaining lifetime in seconds), 0 invalid, -1 redis error.
   A wrong code is left in place. */
int redis_verifycode_consume(const char* email, int code, int* out_ttl);

/* Put a consumed code back. SET NX so a newer code is left in place. */
int redis_verifycode_restore(const char* email, int code, int ttl_seconds);

#endif
