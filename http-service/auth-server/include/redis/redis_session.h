#ifndef REDIS_SESSION_H
#define REDIS_SESSION_H

#include <stdint.h>

#define REDIS_SESSION_TTL 172800

typedef struct {
    uint64_t user_uid;
    uint64_t expires_at;
} session_t;

char* redis_session_create(const session_t* sess);

session_t* redis_session_get(const char* session_id);

int redis_session_refresh(const char* session_id);

int redis_session_delete(const char* session_id);

#endif