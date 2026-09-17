#ifndef POSTGRES_USER_BLOCKS_H
#define POSTGRES_USER_BLOCKS_H

#include "postgres.h"

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

typedef struct {
    uint64_t id;
    uint64_t user_uid;
    time_t created_at;
    time_t blocked_until;
    bool permanent;
    char* reason;
    uint64_t blocked_by_uid;
} user_block_t;

/* Returns the newest active block for a user, or NULL when the user is not blocked. */
user_block_t* db_user_block_get_active(PGconn* conn, uint64_t user_uid);

/* Create a block. blocked_until == 0 means a permanent block. */
db_result_t db_user_block_create(PGconn* conn,
                                 uint64_t user_uid,
                                 time_t blocked_until,
                                 const char* reason,
                                 uint64_t blocked_by_uid);

/* Revoke all currently active blocks for the user. */
db_result_t db_user_block_revoke_active(PGconn* conn,
                                        uint64_t user_uid,
                                        uint64_t revoked_by_uid,
                                        const char* revoke_reason);

void db_user_block_free(user_block_t* block);

#endif
