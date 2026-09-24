#include "postgres/postgres.h"
#include "postgres/postgres_chats.h"

#include "logger.h"

db_result_t db_chat_create(PGconn* conn, chat_t* chat, uint64_t* out_chat_id)
{
    const char* query = "INSERT INTO chats (chat_type, title, description) VALUES ($1, $2, $3) RETURNING id";
    const char* params[3] = {chat->chat_type, chat->title, chat->description ? chat->description : ""};

    time_t start = time(NULL);
    int timeout_sec = 5;

    while (1)
    {
        PGresult* res = db_exec_params(conn, query, 3, params);
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
            *out_chat_id = strtoull(id_str, NULL, 10);

            PQclear(res);
            return DB_OK;
        }

        if (status == PGRES_FATAL_ERROR)
        {
            logger_error("db_chat_create: failed to exec sql: %s", PQresultErrorMessage(res));
            PQclear(res);
            return DB_ERROR;
        }

        PQclear(res);

        if (difftime(time(NULL), start) > timeout_sec)
            return DB_TIMEOUT;
    }
}

db_result_t db_chat_create_members(PGconn* conn, uint64_t chat_id, chat_member_t* members, size_t members_count)
{
    const char* query = "INSERT INTO chat_members (chat_id, user_uid, role) SELECT $1, u, r FROM unnest($2::bigint[], $3::text[]) AS t(u, r)";

    /* convert -> char */
    char chat_id_str[32];
    snprintf(chat_id_str, sizeof(chat_id_str), "%lu", chat_id);

    char user_uids[4096] = "{";
    char roles[4096] = "{";

    for (size_t i = 0; i < members_count; i++)
    {
        char tmp[64];

        snprintf(tmp, sizeof(tmp), "%lu", members[i].user_uid);
        strcat(user_uids, tmp);

        strcat(roles, "\"");
        strcat(roles, members[i].role);
        strcat(roles, "\"");

        if (i + 1 < members_count)
        {
            strcat(user_uids, ",");
            strcat(roles, ",");
        }
    }

    strcat(user_uids, "}");
    strcat(roles, "}");

    const char* params[3] = {chat_id_str, user_uids, roles};

    db_result_t result = execute_sql(conn, query, params, 3);
    if (result != DB_OK)
    {
        logger_error("db_chat_create_members chat_id={%lu}: failed to exec sql: %s", chat_id, PQerrorMessage(conn));
    }

    return result;
}

db_result_t db_chat_create_setting(PGconn* conn, uint64_t chat_id, chat_setting_t* setting)
{
    const char* query = "INSERT INTO chat_settings (chat_id) VALUES ($1)";

    /* convert -> char */
    char chat_id_str[32];
    snprintf(chat_id_str, sizeof(chat_id_str), "%lu", chat_id);

    const char* params[1] = {chat_id_str};

    db_result_t result = execute_sql(conn, query, params, 1);
    if (result != DB_OK)
    {
        logger_error("db_chat_create_setting chat_id={%lu}: failed to exec sql: %s", chat_id, PQerrorMessage(conn));
    }

    return result;
}

db_result_t db_chat_create_all(PGconn* conn, chat_t* chat, chat_member_t* members, size_t members_count, chat_setting_t* setting,
                               uint64_t* out_chat_id)
{
    execute_sql(conn, "BEGIN", NULL, 0);

    db_result_t rc;

    uint64_t chat_id;
    rc = db_chat_create(conn, chat, &chat_id);
    if (rc != DB_OK)
        goto rollback;

    rc = db_chat_create_setting(conn, chat_id, setting);
    if (rc != DB_OK)
        goto rollback;

    rc = db_chat_create_members(conn, chat_id, members, members_count);
    if (rc != DB_OK)
        goto rollback;

    execute_sql(conn, "COMMIT", NULL, 0);

    if (out_chat_id)
        *out_chat_id = chat_id;

    return DB_OK;

rollback:
    logger_error("db_chat_create_all chat_id={%lu}: failed to exec sql: %s", chat_id ? chat_id : 0, PQerrorMessage(conn));

    execute_sql(conn, "ROLLBACK", NULL, 0);

    return DB_ERROR;
}

chat_preview_LIST_t* db_chat_get_my_history(PGconn* conn, uint64_t user_uid, size_t offset)
{
    db_result_set_t* rc = NULL;

    const char* query = "SELECT\n"
                        "    chats.id,\n"
                        "    user_profiles.name,\n"
                        "    user_profiles.username,\n"
                        "    user_profiles.avatar,\n"
                        "    user_cores.user_uid,\n"
                        "    last_message.content AS last_message,\n"
                        "    last_message.created_at AS last_message_date,\n"
                        "    COALESCE(unread_count.count, 0) AS unread_message_count\n"
                        "FROM chats\n"
                        "JOIN chat_members cm_current "
                        "  ON cm_current.chat_id = chats.id AND cm_current.user_uid = $1 "
                        "JOIN chat_members cm_other "
                        "  ON cm_other.chat_id = chats.id AND cm_other.user_uid != $1 "
                        "JOIN user_cores ON user_cores.user_uid = cm_other.user_uid "
                        "LEFT JOIN user_profiles ON user_profiles.user_uid = user_cores.user_uid "
                        "JOIN LATERAL ( "
                        "  SELECT messages.content, messages.created_at "
                        "  FROM messages "
                        "  WHERE messages.chat_id = chats.id "
                        "  ORDER BY messages.created_at DESC "
                        "  LIMIT 1 "
                        ") last_message ON TRUE "
                        "LEFT JOIN LATERAL ( "
                        "  SELECT COUNT(*) AS count "
                        "  FROM messages "
                        "  LEFT JOIN message_statuses "
                        "    ON message_statuses.message_id = messages.id "
                        "    AND message_statuses.receiver_uid = $1 "
                        "  WHERE messages.chat_id = chats.id "
                        "    AND messages.sender_uid != $1 "
                        "    AND message_statuses.read_at IS NULL "
                        ") unread_count ON TRUE "
                        "ORDER BY last_message.created_at DESC NULLS LAST "
                        "LIMIT 15 OFFSET $2";

    /* convert -> char */
    char user_uid_str[32];
    char offset_str[32];

    snprintf(user_uid_str, sizeof(user_uid_str), "%lu", user_uid);
    snprintf(offset_str, sizeof(offset_str), "%zu", offset);

    const char* params[2] = {user_uid_str, offset_str};

    db_result_t result = execute_select(conn, query, params, 2, &rc);
    if (result != DB_OK)
    {
        logger_error("db_chat_get_my_history user_uid={%lu}: failed to exec sql: %s", user_uid, PQerrorMessage(conn));
        free_result_set(rc);
        return NULL;
    }

    if (!rc || rc->n_rows == 0)
    {
        free_result_set(rc);
        return NULL;
    }

    chat_preview_LIST_t* list = malloc(sizeof(chat_preview_LIST_t));
    if (!list)
    {
        logger_error("db_chat_get_my_history user_uid={%lu}: malloc failed for chat_preview_LIST_t", user_uid);

        fprintf(stderr, "malloc failed\n");

        free_result_set(rc);
        return NULL;
    }

    list->count = rc->n_rows;
    list->items = calloc(list->count, sizeof(chat_preview_t));
    if (!list->items)
    {
        logger_error("db_chat_get_my_history user_uid={%lu}: calloc failed for list->items", user_uid);

        fprintf(stderr, "calloc failed\n");

        free(list);
        free_result_set(rc);
        return NULL;
    }

    for (size_t i = 0; i < rc->n_rows; i++)
    {
        chat_preview_t* chat = &list->items[i];

        chat->id = strtoull(rc->rows[i].columns[0], NULL, 10);
        chat->name = parse_pg_strdup(rc->rows[i].columns[1]);
        chat->username = parse_pg_strdup(rc->rows[i].columns[2]);
        chat->avatar = parse_pg_strdup(rc->rows[i].columns[3]);
        chat->user_uid = strtoull(rc->rows[i].columns[4], NULL, 10);
        chat->last_message = parse_pg_strdup(rc->rows[i].columns[5]);
        chat->last_message_date = parse_pg_timestamp(rc->rows[i].columns[6]);
        chat->unread_message_count = strtoull(rc->rows[i].columns[7], NULL, 10);
    }

    free_result_set(rc);
    return list;
}

chat_preview_LIST_t* db_chat_get_by_username(PGconn* conn, uint64_t user_uid, const char* username)
{
    db_result_set_t* rc = NULL;

    const char* query = "SELECT\n"
                        "    user_cores.id,\n"
                        "    user_profiles.name,\n"
                        "    user_profiles.username,\n"
                        "    user_profiles.avatar,\n"
                        "    user_cores.user_uid,\n"
                        "    last_message.content AS last_message,\n"
                        "    last_message.created_at AS last_message_date,\n"
                        "    COALESCE(unread_count.count, 0) AS unread_message_count\n"
                        "FROM user_cores\n"
                        "LEFT JOIN user_profiles ON user_profiles.user_uid = user_cores.user_uid\n"
                        "LEFT JOIN chats\n"
                        "    ON chats.chat_type = 'private'\n"
                        "   AND chats.id IN (\n"
                        "       SELECT chat_id FROM chat_members WHERE user_uid = user_cores.user_uid\n"
                        "       INTERSECT\n"
                        "       SELECT chat_id FROM chat_members WHERE user_uid = $1\n"
                        "   )\n"
                        "LEFT JOIN LATERAL (\n"
                        "    SELECT messages.content, messages.created_at\n"
                        "    FROM messages\n"
                        "    WHERE messages.chat_id = chats.id\n"
                        "    ORDER BY messages.created_at DESC\n"
                        "    LIMIT 1\n"
                        ") AS last_message ON TRUE\n"
                        "LEFT JOIN LATERAL (\n"
                        "    SELECT COUNT(*) AS count\n"
                        "    FROM messages\n"
                        "    LEFT JOIN message_statuses\n"
                        "        ON message_statuses.message_id = messages.id\n"
                        "        AND message_statuses.receiver_uid = $1\n"
                        "    WHERE messages.chat_id = chats.id\n"
                        "      AND messages.sender_uid = user_cores.user_uid\n"
                        "      AND message_statuses.read_at IS NULL\n"
                        ") AS unread_count ON TRUE\n"
                        "WHERE user_cores.user_uid != $1\n"
                        "  AND similarity(user_profiles.username, $2) > 0.6\n"
                        "ORDER BY similarity(user_profiles.username, $2) DESC,\n"
                        "         user_profiles.username ASC";

    /* convert -> char */
    char user_uid_str[32];
    snprintf(user_uid_str, sizeof(user_uid_str), "%lu", user_uid);

    const char* params[2] = {user_uid_str, username};

    db_result_t result = execute_select(conn, query, params, 2, &rc);
    if (result != DB_OK)
    {
        logger_error("db_chat_get_by_username user_uid={%lu}: failed to exec sql: %s", user_uid, PQerrorMessage(conn));
        free_result_set(rc);
        return NULL;
    }

    if (!rc || rc->n_rows == 0)
    {
        free_result_set(rc);
        return NULL;
    }

    chat_preview_LIST_t* list = malloc(sizeof(chat_preview_LIST_t));
    if (!list)
    {
        logger_error("db_chat_get_by_username user_uid={%lu}: malloc failed for list", user_uid);

        fprintf(stderr, "malloc failed\n");

        free_result_set(rc);
        return NULL;
    }

    list->count = rc->n_rows;
    list->items = calloc(list->count, sizeof(chat_preview_t));
    if (!list->items)
    {
        logger_error("db_chat_get_by_username user_uid={%lu}: calloc failed for list->items", user_uid);

        fprintf(stderr, "calloc failed\n");

        free(list);
        free_result_set(rc);
        return NULL;
    }

    for (size_t i = 0; i < rc->n_rows; i++)
    {
        chat_preview_t* chat = &list->items[i];

        chat->id = strtoull(rc->rows[i].columns[0], NULL, 10);
        chat->name = parse_pg_strdup(rc->rows[i].columns[1]);
        chat->username = parse_pg_strdup(rc->rows[i].columns[2]);
        chat->avatar = parse_pg_strdup(rc->rows[i].columns[3]);
        chat->user_uid = strtoull(rc->rows[i].columns[4], NULL, 10);
        chat->last_message = parse_pg_strdup(rc->rows[i].columns[5]);
        chat->last_message_date = parse_pg_timestamp(rc->rows[i].columns[6]);
        chat->unread_message_count = strtoull(rc->rows[i].columns[7], NULL, 10);
    }

    free_result_set(rc);
    return list;
}

chat_member_LIST_t* db_chat_get_members_by_chat_id(PGconn* conn, uint64_t chat_id, uint64_t user_uid)
{
    db_result_set_t* rc = NULL;

    const char* query = "SELECT\n"
                        "   user_uid\n"
                        "FROM chat_members\n"
                        "WHERE chat_id = $1 AND user_uid != $2";

    /* convert -> char */
    char chat_id_str[32];
    char user_uid_str[32];

    snprintf(chat_id_str, sizeof(chat_id_str), "%lu", chat_id);
    snprintf(user_uid_str, sizeof(user_uid_str), "%lu", user_uid);

    const char* params[2] = {chat_id_str, user_uid_str};

    db_result_t result = execute_select(conn, query, params, 2, &rc);
    if (result != DB_OK)
    {
        logger_error("db_chat_get_members_by_chat_id chat_id={%lu} user_uid={%lu}: failed to exec sql: %s", chat_id, user_uid, PQerrorMessage(conn));
        free_result_set(rc);
        return NULL;
    }

    if (!rc || rc->n_rows == 0)
    {
        free_result_set(rc);
        return NULL;
    }

    chat_member_LIST_t* list = malloc(sizeof(chat_member_LIST_t));
    if (!list)
    {
        logger_error("db_chat_get_members_by_chat_id chat_id={%lu} user_uid={%lu}: malloc failed for list", chat_id, user_uid);

        fprintf(stderr, "malloc failed\n");

        free_result_set(rc);
        return NULL;
    }

    list->count = rc->n_rows;
    list->user_uids = calloc(list->count, sizeof(uint64_t));
    if (!list->user_uids)
    {
        logger_error("db_chat_get_members_by_chat_id chat_id={%lu} user_uid={%lu}: calloc failed for list->user_uids", chat_id, user_uid);

        fprintf(stderr, "calloc failed\n");

        free(list);
        free_result_set(rc);
        return NULL;
    }

    for (size_t i = 0; i < rc->n_rows; i++)
    {
        list->user_uids[i] = strtoull(rc->rows[i].columns[0], NULL, 10);
    }

    free_result_set(rc);
    return list;
}

bool db_chat_get_member_exists_by_chat_id(PGconn* conn, uint64_t chat_id, uint64_t user_uid)
{
    db_result_set_t* rc = NULL;

    const char* query = "SELECT EXISTS ("
                        "    SELECT 1"
                        "    FROM chat_members"
                        "    WHERE chat_id = $1 AND user_uid = $2"
                        ")";

    /* convert -> char */
    char chat_id_str[32];
    char user_uid_str[32];

    snprintf(chat_id_str, sizeof(chat_id_str), "%lu", chat_id);
    snprintf(user_uid_str, sizeof(user_uid_str), "%lu", user_uid);

    const char* params[2] = {chat_id_str, user_uid_str};

    db_result_t result = execute_select(conn, query, params, 2, &rc);
    if (result != DB_OK)
    {
        logger_error("db_chat_get_member_exists_by_chat_id chat_id={%lu} user_uid={%lu}: failed to exec sql: %s", chat_id, user_uid,
                     PQerrorMessage(conn));
        free_result_set(rc);
        return false;
    }

    if (!rc || rc->n_rows == 0)
    {
        free_result_set(rc);
        return false;
    }

    const char* query_value = rc->rows[0].columns[0];
    bool exists = (query_value && strcmp(query_value, "t") == 0);

    free_result_set(rc);

    return exists;
}

chat_setting_t* db_chat_get_setting_by_chat_id(PGconn* conn, uint64_t chat_id)
{
    db_result_set_t* rc = NULL;

    const char* query = "SELECT * FROM chat_settings WHERE chat_id = $1";

    /* convert -> char */
    char chat_id_str[32];
    snprintf(chat_id_str, sizeof(chat_id_str), "%lu", chat_id);

    const char* params[1] = {chat_id_str};

    db_result_t result = execute_select(conn, query, params, 1, &rc);
    if (result != DB_OK)
    {
        logger_error("db_chat_get_setting_by_chat_id chat_id={%lu}: failed to exec sql: %s", chat_id, PQerrorMessage(conn));
        free_result_set(rc);
        return NULL;
    }

    if (!rc || rc->n_rows == 0)
    {
        free_result_set(rc);
        return NULL;
    }

    chat_setting_t* chat_setting = malloc(sizeof(chat_setting_t));
    if (!chat_setting)
    {
        logger_error("db_chat_get_setting_by_chat_id chat_id={%lu}: malloc failed for chat_setting", chat_id);

        fprintf(stderr, "malloc failed\n");

        free_result_set(rc);
        return NULL;
    }

    memset(chat_setting, 0, sizeof(chat_setting_t));

    chat_setting->id = strtoull(rc->rows[0].columns[0], NULL, 10);
    chat_setting->created_at = parse_pg_timestamp(rc->rows[0].columns[1]);
    chat_setting->updated_at = parse_pg_timestamp(rc->rows[0].columns[2]);
    chat_setting->chat_id = strtoull(rc->rows[0].columns[3], NULL, 10);
    chat_setting->custom_background = parse_pg_strdup(rc->rows[0].columns[4]);
    chat_setting->blocked = parse_pg_bool(rc->rows[0].columns[5]);
    chat_setting->who_blocked_uid = strtoull(rc->rows[0].columns[6], NULL, 10);

    free_result_set(rc);

    return chat_setting;
}

db_result_t db_chat_upd_cbackground_by_chat_id(PGconn* conn, char* cbackground, uint64_t chat_id)
{
    const char* query = "UPDATE chat_settings SET custom_background = $1 WHERE chat_id = $2";

    /* convert -> chat */
    char chat_id_str[32];
    snprintf(chat_id_str, sizeof(chat_id_str), "%lu", chat_id);

    const char* params[2] = {cbackground, chat_id_str};

    db_result_t result = execute_sql(conn, query, params, 2);
    if (result != DB_OK)
    {
        logger_error("db_chat_upd_cbackground_by_chat_id chat_id={%lu}: failed to exec sql: %s", chat_id, PQerrorMessage(conn));
    }

    return result;
}