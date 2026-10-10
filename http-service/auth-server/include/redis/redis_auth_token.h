#ifndef REDIS_AUTH_TOKEN_H
#define REDIS_AUTH_TOKEN_H

#include <stddef.h>
#include <stdint.h>

typedef enum
{
    REDIS_AUTH_TOKEN_REGISTRATION = 1,
    REDIS_AUTH_TOKEN_LOGIN = 2,
} redis_auth_token_kind_t;

typedef enum
{
    REDIS_AUTH_TOKEN_OK = 0,
    REDIS_AUTH_TOKEN_MISSING = 1,
    REDIS_AUTH_TOKEN_EXPIRED = 2,
    REDIS_AUTH_TOKEN_MISMATCH = 3,
    REDIS_AUTH_TOKEN_ERROR = 4,
} redis_auth_token_status_t;

/* URL-safe token in out_token. Redis stores only SHA-256 of the raw bytes. */
redis_auth_token_status_t redis_auth_token_create(redis_auth_token_kind_t kind, const char* email, char* out_token, size_t out_len);

/* Check binding to email without deleting a valid token. */
redis_auth_token_status_t redis_auth_token_check(redis_auth_token_kind_t kind, const char* token, const char* email);

/* Check binding and delete the key in one Redis operation. */
redis_auth_token_status_t redis_auth_token_consume(redis_auth_token_kind_t kind, const char* token, const char* email);


#endif