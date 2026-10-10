#include "postgres/postgres.h"
#include "postgres/postgres_messages.h"

#include "logger.h"

db_result_t db_message_create(db_t* db, message_t* msg, uint64_t* out_message_id)
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
        PGresult* res = db_exec_params(db, query, 4, params);
        if (!res)
            return db_last_result();

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
            db_result_t fatal = db_fatal_result(res);
            PQclear(res);
            return fatal;
        }

        PQclear(res);

        if (difftime(time(NULL), start) > timeout_sec)
            return DB_TIMEOUT;
    }
}

db_result_t db_message_create_status(db_t* db, uint64_t message_id, uint64_t chat_id, uint64_t sender_uid)
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

    db_result_t result = execute_sql(db, query, params, 3);
    if (result != DB_OK)
    {
        logger_error("db_message_create_status message_id={%lu} chat_id={%lu}: failed to exec sql: %s", message_id, chat_id, db_error_message(db));
    }

    return result;
}

typedef struct
{
    message_t* msg;
    uint64_t message_id;
} db_message_create_all_args_t;

static db_result_t db_message_create_all_tx(db_t* db, void* arg)
{
    db_message_create_all_args_t* args = arg;

    db_result_t rc = db_message_create(db, args->msg, &args->message_id);
    if (rc != DB_OK)
        return rc;

    return db_message_create_status(db, args->message_id, args->msg->chat_id, args->msg->sender_uid);
}

db_result_t db_message_create_all(db_t* db, message_t* msg)
{
    db_message_create_all_args_t args = {
        .msg = msg,
        .message_id = 0,
    };

    db_result_t rc = db_transaction(db, db_message_create_all_tx, &args);
    if (rc != DB_OK)
        logger_error("db_message_create_all message_id={%lu}: failed to exec sql: %s", args.message_id, db_error_message(db));

    return rc;
}