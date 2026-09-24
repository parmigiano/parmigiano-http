#include "postgres/postgres.h"
#include "postgres/postgres_messages.h"

#include "logger.h"

db_result_t db_message_create(PGconn* conn, message_t* msg, uint64_t* out_message_id)
{
    const char* query = "INSERT INTO messages (chat_id, sender_uid, content, content_type) VALUES ($1, $2, $3, $4) RETURNING id;";

    char chat_id_str[32];
    char sender_uid_str[32];

    snprintf(chat_id_str, sizeof(chat_id_str), "%lu", msg->chat_id);
    snprintf(sender_uid_str, sizeof(sender_uid_str), "%lu", msg->sender_uid);

    const char* params[4] = {chat_id_str, sender_uid_str, msg->content, msg->content_type};

    time_t start = time(NULL);
    int timeout_sec = 5;

    while (1)
    {
        PGresult* res = db_exec_params(conn, query, 4, params);
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
            *out_message_id = strtoull(id_str, NULL, 10);

            PQclear(res);
            return DB_OK;
        }

        if (status == PGRES_FATAL_ERROR)
        {
            logger_error("db_message_create chat_id={%lu} sender_uid={%lu}: failed to exec sql: %s", msg->chat_id ? msg->chat_id : 0,
                         msg->sender_uid ? msg->sender_uid : 0, PQresultErrorMessage(res));
            PQclear(res);
            return DB_ERROR;
        }

        PQclear(res);

        if (difftime(time(NULL), start) > timeout_sec)
            return DB_TIMEOUT;
    }
}

db_result_t db_message_create_status(PGconn* conn, uint64_t message_id, uint64_t chat_id, uint64_t sender_uid)
{
    const char* query = "INSERT INTO message_statuses (message_id, receiver_uid)\n"
                        "SELECT $1, chat_members.user_uid\n"
                        "FROM chat_members\n"
                        "WHERE chat_members.chat_id = $2\n"
                        "   AND chat_members.user_uid <> $3";

    char message_id_str[32];
    char chat_id_str[32];
    char sender_uid_str[32];

    snprintf(message_id_str, sizeof(message_id_str), "%lu", message_id);
    snprintf(chat_id_str, sizeof(chat_id_str), "%lu", chat_id);
    snprintf(sender_uid_str, sizeof(sender_uid_str), "%lu", sender_uid);

    const char* params[3] = {message_id_str, chat_id_str, sender_uid_str};

    db_result_t result = execute_sql(conn, query, params, 3);
    if (result != DB_OK)
    {
        logger_error("db_message_create_status message_id={%lu} chat_id={%lu}: failed to exec sql: %s", message_id, chat_id, PQerrorMessage(conn));
    }

    return result;
}

db_result_t db_message_create_all(PGconn* conn, message_t* msg)
{
    execute_sql(conn, "BEGIN", NULL, 0);

    db_result_t rc;

    uint64_t message_id;
    rc = db_message_create(conn, msg, &message_id);
    if (rc != DB_OK)
        goto rollback;

    rc = db_message_create_status(conn, message_id, msg->chat_id, msg->sender_uid);
    if (rc != DB_OK)
        goto rollback;

    execute_sql(conn, "COMMIT", NULL, 0);

    return DB_OK;

rollback:
    logger_error("db_message_create_all message_id={%lu}: failed to exec sql: %s", message_id ? message_id : 0, PQerrorMessage(conn));

    execute_sql(conn, "ROLLBACK", NULL, 0);

    return DB_ERROR;
}