#include "postgres/postgres.h"
#include "postgres/postgres_group_chats.h"

#include "logger.h"

/* Create chat group and return chat id */
db_result_t db_group_chat_create(PGconn* conn, chat_group_t* chat_group, uint64_t* out_chat_group_id)
{
    const char* query = "INSERT INTO chat_groups (user_uid, name) VALUES ($1, $2) RETURNING id";

    /* convert -> char */
    char user_uid_str[32];
    snprintf(user_uid_str, sizeof(user_uid_str), "%lu", chat_group->user_uid);

    const char* params[2] = {user_uid_str, chat_group->name};

    time_t start = time(NULL);
    int timeout_sec = 5;

    while (1)
    {
        PGresult* res = db_exec_params(conn, query, 2, params);
        if (!res)
            return DB_ERROR;

        ExecStatusType status = PQresultStatus(res);

        if (status == PGRES_TUPLES_OK)
        {
            if (PQntuples(res) != 1)
            {
                PQclear(res);
                return DB_ERROR;
            }

            const char* id_str = PQgetvalue(res, 0, 0);
            *out_chat_group_id = strtoull(id_str, NULL, 10);

            PQclear(res);
            return DB_OK;
        }

        if (status == PGRES_FATAL_ERROR)
        {
            logger_error("db_group_chat_create: failed to exec sql: %s", PQresultErrorMessage(res));
            PQclear(res);
            return DB_ERROR;
        }

        PQclear(res);

        if (difftime(time(NULL), start) > timeout_sec)
            return DB_TIMEOUT;
    }
}

db_result_t db_group_chat_add_chats(PGconn* conn, uint64_t user_uid, uint64_t group_id, int* chat_ids, size_t chat_size)
{
    const char* query = "INSERT INTO chat_group_user_chats (user_uid, group_id, chat_id) "
                        "SELECT $1, $2, UNNEST($3::bigint[]) "
                        "ON CONFLICT DO NOTHING";

    /* convert -> char */
    char user_uid_str[32];
    snprintf(user_uid_str, sizeof(user_uid_str), "%lu", user_uid);

    char group_id_str[32];
    snprintf(group_id_str, sizeof(group_id_str), "%lu", group_id);

    char chats_array[2048];
    size_t offset = 0;

    chats_array[offset++] = '{';

    for (size_t i = 0; i < chat_size; i++)
    {
        int written = snprintf(chats_array + offset, sizeof(chats_array) - offset, "%s%u", (i == 0 ? "" : ","), chat_ids[i]);

        if (written < 0 || (size_t)written >= sizeof(chats_array) - offset)
            return DB_ERROR;

        offset += written;
    }

    chats_array[offset++] = '}';
    chats_array[offset] = '\0';

    const char* params[3] = {user_uid_str, group_id_str, chats_array};

    db_result_t result = execute_sql(conn, query, params, 3);
    if (result != DB_OK)
    {
        logger_error("db_group_chat_add_chats user_uid={%lu}: failed to exec sql: %s", user_uid, PQerrorMessage(conn));
    }

    return result;
}

/* Edit name chat group */
db_result_t db_group_chat_edit_name_by_group_id(PGconn* conn, uint64_t group_id, uint64_t user_uid, char* name)
{
    const char* query = "UPDATE chat_groups SET name = $1\n"
                        "WHERE id = $2\n"
                        "   AND user_uid = $3";

    /* convert -> str */
    char group_id_str[32];
    char user_uid_str[32];

    snprintf(group_id_str, sizeof(group_id_str), "%lu", group_id);
    snprintf(user_uid_str, sizeof(user_uid_str), "%lu", user_uid);

    const char* params[3] = {name, group_id_str, user_uid_str};

    db_result_t result = execute_sql(conn, query, params, 3);
    if (result != DB_OK)
    {
        logger_error("db_group_chat_edit_name_by_group_id user_uid={%lu}: failed to exec sql: %s", user_uid, PQerrorMessage(conn));
    }

    return result;
}

/* Update chats in group */
db_result_t db_group_chat_edit_chats_by_group_id(PGconn* conn, uint64_t group_id, uint64_t user_uid, int* chat_ids, size_t chat_size)
{
    const char* query = "WITH new_chats AS (\n"
                        "    SELECT UNNEST($3::bigint[]) AS chat_id\n"
                        "), deleted AS (\n"
                        "    DELETE FROM chat_group_user_chats\n"
                        "    WHERE user_uid = $1\n"
                        "      AND group_id = $2\n"
                        "      AND chat_id NOT IN (SELECT chat_id FROM new_chats)\n"
                        ")\n"
                        "INSERT INTO chat_group_user_chats (user_uid, group_id, chat_id)\n"
                        "SELECT $1, $2, chat_id\n"
                        "FROM new_chats\n"
                        "ON CONFLICT DO NOTHING";

    /* convert -> str */
    char group_id_str[32];
    char user_uid_str[32];

    snprintf(group_id_str, sizeof(group_id_str), "%lu", group_id);
    snprintf(user_uid_str, sizeof(user_uid_str), "%lu", user_uid);

    char chats_array[2048];
    size_t offset = 0;

    chats_array[offset++] = '{';

    for (size_t i = 0; i < chat_size; i++)
    {
        int written = snprintf(chats_array + offset, sizeof(chats_array) - offset, "%s%u", (i == 0 ? "" : ","), chat_ids[i]);

        if (written < 0 || (size_t)written >= sizeof(chats_array) - offset)
            return DB_ERROR;

        offset += written;
    }

    chats_array[offset++] = '}';
    chats_array[offset] = '\0';

    const char* params[3] = {user_uid_str, group_id_str, chats_array};

    db_result_t result = execute_sql(conn, query, params, 3);
    if (result != DB_OK)
    {
        logger_error("db_group_chat_edit_chats_by_group_id user_uid={%lu}: failed to exec sql: %s", user_uid, PQerrorMessage(conn));
    }

    return result;
}

db_result_t db_group_chat_delete(PGconn* conn, uint64_t user_uid, uint64_t group_id)
{
    const char* query = "WITH deleted_links AS (\n"
                        "   DELETE FROM chat_group_user_chats\n"
                        "   WHERE group_id = $1 AND user_uid = $2\n"
                        ")\n"
                        "DELETE FROM chat_groups\n"
                        "WHERE id = $1 AND user_uid = $2";

    char group_id_str[32];
    char user_uid_str[32];

    snprintf(group_id_str, sizeof(group_id_str), "%lu", group_id);
    snprintf(user_uid_str, sizeof(user_uid_str), "%lu", user_uid);

    const char* params[2] = {group_id_str, user_uid_str};

    db_result_t result = execute_sql(conn, query, params, 2);
    if (result != DB_OK)
    {
        logger_error("db_group_delete user_uid={%lu}: failed to exec sql: %s", user_uid, PQerrorMessage(conn));
    }

    return result;
}

// chat_group_LIST_t* db_group_chat_get_list(PGconn* conn, uint64_t user_uid)
// {
//     const char* query = "SELECT\n"
//                         "   chat_groups.id,\n"
//                         "   chat_groups.name,\n"
//                         "   chat_group_user_chats.chat_id\n"
//                         "FROM chat_groups\n"
//                         "LEFT JOIN chat_group_user_chats\n"
//                         "  ON chat_group_user_chats.group_id = chat_groups.id\n"
//                         "       AND chat_group_user_chats.user_uid = $1\n"
//                         "WHERE chat_groups.user_uid = $1\n"
//                         "ORDER BY chat_groups.id";

//     char user_uid_str[32];
//     snprintf(user_uid_str, sizeof(user_uid_str), "%lu", user_uid);

//     const char* params[1] = {user_uid_str};

//     db_result_set_t* rc = NULL;

//     db_result_t result = execute_select(conn, query, params, 1, &rc);
//     if (result != DB_OK)
//     {
//         logger_error("db_group_chat_get_list user_uid={%lu}: failed to exec sql: %s", user_uid, PQerrorMessage(conn));
//         free_result_set(rc);
//         return NULL;
//     }

//     if (!rc || rc->n_rows == 0)
//     {
//         free_result_set(rc);
//         return NULL;
//     }

//     chat_group_LIST_t* list = malloc(sizeof(chat_group_LIST_t));
//     if (!list)
//     {
//         logger_error("db_group_chat_get_list user_uid={%lu}: malloc failed for chat_group_LIST_t", user_uid);

//         fprintf(stderr, "malloc failed\n");

//         free_result_set(rc);
//         return NULL;
//     }

//     list->count = rc->n_rows;
//     list->items = calloc(list->count, sizeof(chat_group_all_t));
//     if (!list->items)
//     {
//         logger_error("db_group_chat_get_list user_uid={%lu}: calloc failed for list->items", user_uid);

//         fprintf(stderr, "calloc failed\n");

//         free(list);
//         free_result_set(rc);
//         return NULL;
//     }

//     for (size_t i = 0; i < rc->n_rows; i++)
//     {
//         chat_group_all_t* chat_group = &list->items[i];

//         chat_group->id = strtoull(rc->rows[i].columns[0], NULL, 10);
//         chat_group->created_at = parse_pg_timestamp(rc->rows[i].columns[1]);
//         chat_group->user_uid = strtoull(rc->rows[i].columns[2], NULL, 10);
//         chat_group->name = parse_pg_strdup(rc->rows[i].columns[3]);
//     }
// }