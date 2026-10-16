#ifndef REDIS_USER_CACHE_H
#define REDIS_USER_CACHE_H

#include "postgres/postgres_users.h"
#include "postgres/postgres_user_blocks.h"

#define REDIS_USER_INFO_TTL 90
#define REDIS_USER_BLOCK_TTL 90

/* 1 = hit, 0 = miss, -1 = redis error. *out is set only on a hit. */
int redis_user_info_get(uint64_t user_uid, db_user_info_t** out);

/* 0 = stored, -1 = failed. */
int redis_user_info_set(const db_user_info_t* user);

/* 0 = deleted or already absent, -1 = failed. */
int redis_user_info_delete(uint64_t user_uid);

/* 1 = hit, 0 = miss, -1 = redis error. *out_blocked is set only on a hit. */
int redis_user_block_get(uint64_t user_uid, int* out_blocked);

/* block == NULL stores "not blocked". 0 = stored, -1 = failed. */
int redis_user_block_set(uint64_t user_uid, const db_user_block_t* block);

/* 0 = deleted or already absent, -1 = failed. */
int redis_user_block_delete(uint64_t user_uid);

#endif
