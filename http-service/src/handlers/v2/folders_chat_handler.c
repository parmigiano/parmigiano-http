#include "handlers.h"

#include "httpx.h"
#include "postgres/postgres.h"
#include "postgres/postgres_group_chats.h"

#include <cjson/cJSON.h>

#define MAX_COUNT_CHAT_IDs 65

typedef struct
{
    char* name;
    chttpx_number_array_t chat_ids;
} group_chats_create_t;

typedef struct
{
    char* name;
    chttpx_number_array_t chat_ids;
} group_chats_edit_t;

void group_chats_create_handler_v2(chttpx_request_t* req, chttpx_response_t* res)
{
    /* Context in request */
    auth_token_t* ctx = (auth_token_t*)req->context;

    group_chats_create_t payload = {0};

    chttpx_validation_t fields[] = {
        chttpx_validation_string("name", &payload.name, true, 0, 20, VALIDATOR_NONE),
        {.name = "chat_ids", .type = FIELD_NUMBER_ARRAY, .target = &payload.chat_ids},
    };

    if (!cHTTPX_Parse(req, fields, (sizeof(fields) / sizeof(fields[0]))))
        goto errorjson;

    if (!cHTTPX_Validate(req, fields, (sizeof(fields) / sizeof(fields[0])), ctx->lang))
        goto errorjson;

    if (payload.chat_ids.count > MAX_COUNT_CHAT_IDs)
    {
        *res = cHTTPX_ResJson(cHTTPX_StatusBadRequest, "{\"error\": \"%s\"}", cHTTPX_i18n_t("error.limit-chat-group", ctx->lang));
        goto cleanup;
    }

    /* GroupID returned before created group */
    uint64_t group_id = 0;

    chat_group_t group = {.user_uid = ctx->user->user_uid, .name = payload.name};

    PGconn* conn = http_server->conn;
    PGresult* pg_res = NULL;

    /* Begin transation db */
    pg_res = db_exec(conn, "BEGIN");
    if (!pg_res || PQresultStatus(pg_res) != PGRES_COMMAND_OK)
    {
        PQclear(pg_res);
        *res = cHTTPX_ResJson(cHTTPX_StatusInternalServerError, "{\"error\": \"%s\"}", cHTTPX_i18n_t("error.perform-database-operation", ctx->lang));
        goto cleanup;
    }
    PQclear(pg_res);

    db_result_t r = db_group_chat_create(conn, &group, &group_id);
    if (r != DB_OK || group_id == 0)
        goto rollback;

    r = db_group_chat_add_chats(http_server->conn, ctx->user->user_uid, group_id, payload.chat_ids.items, payload.chat_ids.count);
    if (r != DB_OK)
        goto rollback;

    pg_res = db_exec(conn, "COMMIT");
    if (!pg_res || PQresultStatus(pg_res) != PGRES_COMMAND_OK)
    {
        PQclear(pg_res);
        *res = cHTTPX_ResJson(cHTTPX_StatusInternalServerError, "{\"error\": \"%s\"}", cHTTPX_i18n_t("error.perform-database-operation", ctx->lang));
        goto cleanup;
    }
    PQclear(pg_res);

    /* Build JSON response */
    char chat_ids_json[2048];
    size_t offset = 0;

    offset += snprintf(chat_ids_json + offset, sizeof(chat_ids_json) - offset, "[");

    for (size_t i = 0; i < payload.chat_ids.count; i++)
    {
        offset += snprintf(chat_ids_json + offset, sizeof(chat_ids_json) - offset, "%s%d", (i == 0 ? "" : ","), payload.chat_ids.items[i]);
    }

    snprintf(chat_ids_json + offset, sizeof(chat_ids_json) - offset, "]");

    *res = cHTTPX_ResJson(cHTTPX_StatusCreated, "{\"message\": {\"id\": \"%ld\", \"name\": \"%s\", \"chat_ids\": %s}}", group_id, payload.name,
                          chat_ids_json);

    goto cleanup;

rollback:
    pg_res = db_exec(conn, "ROLLBACK");
    PQclear(pg_res);

    switch (r)
    {
    case DB_TIMEOUT:
        *res = cHTTPX_ResJson(cHTTPX_StatusConnectionTimedOut, "{\"error\": \"%s\"}", cHTTPX_i18n_t("error.database-connection-timeout", ctx->lang));
        goto cleanup;

    case DB_DUPLICATE:
        *res = cHTTPX_ResJson(cHTTPX_StatusBadRequest, "{\"error\": \"%s\"}", cHTTPX_i18n_t("error.repeating-data-request", ctx->lang));
        goto cleanup;

    case DB_ERROR:
        *res = cHTTPX_ResJson(cHTTPX_StatusInternalServerError, "{\"error\": \"%s\"}", cHTTPX_i18n_t("error.perform-database-operation", ctx->lang));
        goto cleanup;
    }

cleanup:
    free(payload.name);

    return;

errorjson:
    *res = cHTTPX_ResJson(cHTTPX_StatusBadRequest, "{\"error\": \"%s\"}", req->error_msg);

    goto cleanup;
}

void group_chats_edit_handler_v2(chttpx_request_t* req, chttpx_response_t* res)
{
    /* Context in request */
    auth_token_t* ctx = (auth_token_t*)req->context;

    const char* group_id_param = cHTTPX_Param(req, "group_id");

    uint64_t group_id = 0;
    if (group_id_param && *group_id_param != '\0')
        group_id = strtoull(group_id_param, NULL, 10);

    group_chats_edit_t payload = {0};

    chttpx_validation_t fields[] = {
        chttpx_validation_string("name", &payload.name, true, 0, 20, VALIDATOR_NONE),
        {.name = "chat_ids", .type = FIELD_NUMBER_ARRAY, .target = &payload.chat_ids},
    };

    if (!cHTTPX_Parse(req, fields, (sizeof(fields) / sizeof(fields[0]))))
        goto errorjson;

    if (!cHTTPX_Validate(req, fields, (sizeof(fields) / sizeof(fields[0])), ctx->lang))
        goto errorjson;

    if (payload.chat_ids.count > MAX_COUNT_CHAT_IDs)
    {
        *res = cHTTPX_ResJson(cHTTPX_StatusBadRequest, "{\"error\": \"%s\"}", cHTTPX_i18n_t("error.limit-chat-group", ctx->lang));
        goto cleanup;
    }

    PGconn* conn = http_server->conn;
    PGresult* pg_res = NULL;

    /* Begin transation db */
    pg_res = db_exec(conn, "BEGIN");
    if (!pg_res || PQresultStatus(pg_res) != PGRES_COMMAND_OK)
    {
        PQclear(pg_res);
        *res = cHTTPX_ResJson(cHTTPX_StatusInternalServerError, "{\"error\": \"%s\"}", cHTTPX_i18n_t("error.perform-database-operation", ctx->lang));
        goto cleanup;
    }
    PQclear(pg_res);

    db_result_t r = db_group_chat_edit_name_by_group_id(conn, group_id, ctx->user->user_uid, payload.name);
    if (r != DB_OK)
        goto rollback;

    r = db_group_chat_edit_chats_by_group_id(conn, group_id, ctx->user->user_uid, payload.chat_ids.items, payload.chat_ids.count);
    if (r != DB_OK)
        goto rollback;

    pg_res = db_exec(conn, "COMMIT");
    if (!pg_res || PQresultStatus(pg_res) != PGRES_COMMAND_OK)
    {
        PQclear(pg_res);
        *res = cHTTPX_ResJson(cHTTPX_StatusInternalServerError, "{\"error\": \"%s\"}", cHTTPX_i18n_t("error.perform-database-operation", ctx->lang));
        goto cleanup;
    }
    PQclear(pg_res);

    /* Build JSON response */
    char chat_ids_json[2048];
    size_t offset = 0;

    offset += snprintf(chat_ids_json + offset, sizeof(chat_ids_json) - offset, "[");

    for (size_t i = 0; i < payload.chat_ids.count; i++)
    {
        offset += snprintf(chat_ids_json + offset, sizeof(chat_ids_json) - offset, "%s%d", (i == 0 ? "" : ","), payload.chat_ids.items[i]);
    }

    snprintf(chat_ids_json + offset, sizeof(chat_ids_json) - offset, "]");

    *res = cHTTPX_ResJson(cHTTPX_StatusOK, "{\"message\": {\"id\": \"%ld\", \"name\": \"%s\", \"chat_ids\": %s}}", group_id, payload.name,
                          chat_ids_json);

    goto cleanup;

rollback:
    pg_res = db_exec(conn, "ROLLBACK");
    PQclear(pg_res);

    switch (r)
    {
    case DB_TIMEOUT:
        *res = cHTTPX_ResJson(cHTTPX_StatusConnectionTimedOut, "{\"error\": \"%s\"}", cHTTPX_i18n_t("error.database-connection-timeout", ctx->lang));
        goto cleanup;

    case DB_DUPLICATE:
        *res = cHTTPX_ResJson(cHTTPX_StatusBadRequest, "{\"error\": \"%s\"}", cHTTPX_i18n_t("error.repeating-data-request", ctx->lang));
        goto cleanup;

    case DB_ERROR:
        *res = cHTTPX_ResJson(cHTTPX_StatusInternalServerError, "{\"error\": \"%s\"}", cHTTPX_i18n_t("error.perform-database-operation", ctx->lang));
        goto cleanup;
    }

cleanup:
    free(payload.name);

    return;

errorjson:
    *res = cHTTPX_ResJson(cHTTPX_StatusBadRequest, "{\"error\": \"%s\"}", req->error_msg);

    goto cleanup;
}

void group_chats_delete_handler_v2(chttpx_request_t* req, chttpx_response_t* res)
{
    /* Context in request */
    auth_token_t* ctx = (auth_token_t*)req->context;

    const char* group_id_param = cHTTPX_Param(req, "group_id");

    uint64_t group_id = 0;
    if (group_id_param && *group_id_param != '\0')
        group_id = strtoull(group_id_param, NULL, 10);

    db_result_t db_group_delete = db_group_chat_delete(http_server->conn, ctx->user->user_uid, group_id);

    switch (db_group_delete)
    {
    case DB_TIMEOUT:
        *res = cHTTPX_ResJson(cHTTPX_StatusConnectionTimedOut, "{\"error\": \"%s\"}", cHTTPX_i18n_t("error.database-connection-timeout", ctx->lang));
        goto cleanup;

    case DB_DUPLICATE:
        *res = cHTTPX_ResJson(cHTTPX_StatusBadRequest, "{\"error\": \"%s\"}", cHTTPX_i18n_t("error.repeating-data-request", ctx->lang));
        goto cleanup;

    case DB_ERROR:
        *res = cHTTPX_ResJson(cHTTPX_StatusInternalServerError, "{\"error\": \"%s\"}", cHTTPX_i18n_t("error.perform-database-operation", ctx->lang));
        goto cleanup;
    }

    *res = cHTTPX_ResJson(cHTTPX_StatusNoContent, NULL);

cleanup:
    return;
}