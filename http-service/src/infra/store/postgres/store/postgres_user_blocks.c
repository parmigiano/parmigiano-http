#include "postgres/postgres_user_blocks.h"

#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

user_block_t* db_user_block_get_active(PGconn* conn, uint64_t user_uid)
{
    db_result_set_t* rc = NULL;

    const char* query =
        "SELECT id, user_uid, created_at, blocked_until, reason, blocked_by_uid "
        "FROM user_blocks "
        "WHERE user_uid = $1 "
        "  AND revoked_at IS NULL "
        "  AND (blocked_until IS NULL OR blocked_until > now()) "
        "ORDER BY created_at DESC "
        "LIMIT 1";

    char user_uid_str[32];
    snprintf(user_uid_str, sizeof(user_uid_str), "%lu", user_uid);

    const char* params[1] = {user_uid_str};

    if (execute_select(conn, query, params, 1, &rc) != DB_OK)
    {
        logger_error("db_user_block_get_active user_uid={%lu}: failed to exec sql: %s", user_uid, PQerrorMessage(conn));
        free_result_set(rc);
        return NULL;
    }

    if (!rc || rc->n_rows == 0)
    {
        free_result_set(rc);
        return NULL;
    }

    user_block_t* block = calloc(1, sizeof(*block));
    if (!block)
    {
        free_result_set(rc);
        return NULL;
    }

    block->id = strtoull(rc->rows[0].columns[0], NULL, 10);
    block->user_uid = strtoull(rc->rows[0].columns[1], NULL, 10);
    block->created_at = parse_pg_timestamp(rc->rows[0].columns[2]);
    block->permanent = rc->rows[0].columns[3][0] == '\0';
    block->blocked_until = block->permanent ? 0 : parse_pg_timestamp(rc->rows[0].columns[3]);
    block->reason = parse_pg_strdup(rc->rows[0].columns[4]);
    block->blocked_by_uid = rc->rows[0].columns[5][0] == '\0' ? 0 : strtoull(rc->rows[0].columns[5], NULL, 10);

    free_result_set(rc);
    return block;
}

db_result_t db_user_block_create(PGconn* conn,
                                 uint64_t user_uid,
                                 time_t blocked_until,
                                 const char* reason,
                                 uint64_t blocked_by_uid)
{
    if (!conn || !user_uid || !reason || !*reason)
        return DB_ERROR;

    const char* query =
        "INSERT INTO user_blocks (user_uid, blocked_until, reason, blocked_by_uid) "
        "VALUES ($1, CASE WHEN $2 = '' THEN NULL ELSE to_timestamp($2::bigint) END, $3, "
        "        CASE WHEN $4 = '0' THEN NULL ELSE $4::bigint END)";

    char user_uid_str[32];
    char blocked_until_str[32] = {0};
    char blocked_by_uid_str[32];

    snprintf(user_uid_str, sizeof(user_uid_str), "%lu", user_uid);
    if (blocked_until > 0)
        snprintf(blocked_until_str, sizeof(blocked_until_str), "%lld", (long long)blocked_until);
    snprintf(blocked_by_uid_str, sizeof(blocked_by_uid_str), "%lu", blocked_by_uid);

    const char* params[4] = {user_uid_str, blocked_until_str, reason, blocked_by_uid_str};
    return execute_sql(conn, query, params, 4);
}

db_result_t db_user_block_revoke_active(PGconn* conn,
                                        uint64_t user_uid,
                                        uint64_t revoked_by_uid,
                                        const char* revoke_reason)
{
    if (!conn || !user_uid)
        return DB_ERROR;

    const char* query =
        "UPDATE user_blocks "
        "SET revoked_at = now(), "
        "    revoked_by_uid = CASE WHEN $2 = '0' THEN NULL ELSE $2::bigint END, "
        "    revoke_reason = NULLIF($3, '') "
        "WHERE user_uid = $1 "
        "  AND revoked_at IS NULL "
        "  AND (blocked_until IS NULL OR blocked_until > now())";

    char user_uid_str[32];
    char revoked_by_uid_str[32];

    snprintf(user_uid_str, sizeof(user_uid_str), "%lu", user_uid);
    snprintf(revoked_by_uid_str, sizeof(revoked_by_uid_str), "%lu", revoked_by_uid);

    const char* params[3] = {user_uid_str, revoked_by_uid_str, revoke_reason ? revoke_reason : ""};
    return execute_sql(conn, query, params, 3);
}

void db_user_block_free(user_block_t* block)
{
    if (!block)
        return;

    free(block->reason);
    free(block);
}
