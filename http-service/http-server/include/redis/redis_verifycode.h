#ifndef REDIS_VERIFYCODE_H
#define REDIS_VERIFYCODE_H

#include <stdbool.h>

#define REDIS_VERIFYCODE_TTL 1800

int redis_verifycode_create(const char* email, int code);

bool redis_verifycode_get(const char* email, int* out_code);

#endif
