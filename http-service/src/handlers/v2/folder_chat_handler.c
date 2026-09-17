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
    auth_token_t* ctx = cHTTPX_ContextGet(req, AUTH_CONTEXT_NAME);
    if (!ctx || !ctx->user)
    {
        *res = cHTTPX_ResError(cHTTPX_StatusUnauthorized, cHTTPX_i18n_t("error.connect-to-account", req->language));
        return;
    }

    group_chats_create_t payload = {0};

    chttpx_validation_t fields[] = {
        cHTTPX_StringField("name", &payload.name, true, 0, 20, CHTTPX_TRIM, NULL),
        {.name = "chat_ids", .type = FIELD_NUMBER_ARRAY, .target = &payload.chat_ids},
    };

    if (!bind_json_i18n(req, res, fields, CHTTPX_ARRAY_LEN(fields)))
        return;

    if (payload.chat_ids.count > MAX_COUNT_CHAT_IDs)
    {
        *res = cHTTPX_ResError(cHTTPX_StatusBadRequest, cHTTPX_i18n_t("error.limit-chat-group", req->language));
        return;
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
        *res = cHTTPX_ResError(cHTTPX_StatusInternalServerError, cHTTPX_i18n_t("error.perform-database-operation", req->language));
        return;
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
        *res = cHTTPX_ResError(cHTTPX_StatusInternalServerError, cHTTPX_i18n_t("error.perform-database-operation", req->language));
        return;
    }
    PQclear(pg_res);

    chttpx_json_t* root = cHTTPX_JsonObject(req);
	chttpx_json_t* message = cHTTPX_JsonObject(req);
	chttpx_json_t* ids = cHTTPX_JsonArray(req);

	if (!root || !message || !ids || cHTTPX_JsonNumber(message, "id", (double)group_id) != 0 || cHTTPX_JsonString(message, "name", payload.name) != 0)
	{
		*res = cHTTPX_ResError(cHTTPX_StatusInternalServerError, cHTTPX_i18n_t("error.something-went-wrong", req->language));
		return;
	}

	for (size_t i = 0; i < payload.chat_ids.count; ++i)
	{
		if (cHTTPX_JsonArrayNumber(ids, payload.chat_ids.items[i]) != 0)
		{
			*res = cHTTPX_ResError(cHTTPX_StatusInternalServerError, cHTTPX_i18n_t("error.something-went-wrong", req->language));
			return;
		}
	}

	if (cHTTPX_JsonChild(message, "chat_ids", ids) != 0 || cHTTPX_JsonChild(root, "message", message) != 0)
	{
		*res = cHTTPX_ResError(cHTTPX_StatusInternalServerError, cHTTPX_i18n_t("error.something-went-wrong", req->language));
		return;
	}

	*res = cHTTPX_ResJsonObject(cHTTPX_StatusCreated, root);
	return;

rollback:
    pg_res = db_exec(conn, "ROLLBACK");
    PQclear(pg_res);

    switch (r)
    {
    case DB_OK:
        break;

    case DB_TIMEOUT:
        *res = cHTTPX_ResError(cHTTPX_StatusConnectionTimedOut, cHTTPX_i18n_t("error.database-connection-timeout", req->language));
        return;

    case DB_DUPLICATE:
        *res = cHTTPX_ResError(cHTTPX_StatusBadRequest, cHTTPX_i18n_t("error.repeating-data-request", req->language));
        return;

    case DB_ERROR:
        *res = cHTTPX_ResError(cHTTPX_StatusInternalServerError, cHTTPX_i18n_t("error.perform-database-operation", req->language));
        return;
    }
}

void group_chats_edit_handler_v2(chttpx_request_t* req, chttpx_response_t* res)
{
    /* Context in request */
    auth_token_t* ctx = cHTTPX_ContextGet(req, AUTH_CONTEXT_NAME);
    if (!ctx || !ctx->user)
    {
        *res = cHTTPX_ResError(cHTTPX_StatusUnauthorized, cHTTPX_i18n_t("error.connect-to-account", req->language));
        return;
    }

    uint64_t group_id = 0;
    if (!cHTTPX_ParamU64(req, "group_id", &group_id))
    {
        *res = cHTTPX_ResError(cHTTPX_StatusBadRequest, cHTTPX_i18n_t("error.invalid-group-id", req->language));
        return;
    }

    group_chats_edit_t payload = {0};

    chttpx_validation_t fields[] = {
        cHTTPX_StringField("name", &payload.name, true, 0, 20, CHTTPX_TRIM, NULL),
        {.name = "chat_ids", .type = FIELD_NUMBER_ARRAY, .target = &payload.chat_ids},
    };

    if (!bind_json_i18n(req, res, fields, CHTTPX_ARRAY_LEN(fields)))
        return;

    if (payload.chat_ids.count > MAX_COUNT_CHAT_IDs)
    {
        *res = cHTTPX_ResError(cHTTPX_StatusBadRequest, cHTTPX_i18n_t("error.limit-chat-group", req->language));
        return;
    }

    PGconn* conn = http_server->conn;
    PGresult* pg_res = NULL;

    /* Begin transation db */
    pg_res = db_exec(conn, "BEGIN");
    if (!pg_res || PQresultStatus(pg_res) != PGRES_COMMAND_OK)
    {
        PQclear(pg_res);
        *res = cHTTPX_ResError(cHTTPX_StatusInternalServerError, cHTTPX_i18n_t("error.perform-database-operation", req->language));
        return;
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
        *res = cHTTPX_ResError(cHTTPX_StatusInternalServerError, cHTTPX_i18n_t("error.perform-database-operation", req->language));
        return;
    }
    PQclear(pg_res);

	chttpx_json_t* root = cHTTPX_JsonObject(req);
	chttpx_json_t* message = cHTTPX_JsonObject(req);
	chttpx_json_t* ids = cHTTPX_JsonArray(req);

	if (!root || !message || !ids || cHTTPX_JsonNumber(message, "id", (double)group_id) != 0 || cHTTPX_JsonString(message, "name", payload.name) != 0)
	{
		*res = cHTTPX_ResError(cHTTPX_StatusInternalServerError, cHTTPX_i18n_t("error.something-went-wrong", req->language));
		return;
	}

	for (size_t i = 0; i < payload.chat_ids.count; ++i)
	{
		if (cHTTPX_JsonArrayNumber(ids, payload.chat_ids.items[i]) != 0)
		{
			*res = cHTTPX_ResError(cHTTPX_StatusInternalServerError, cHTTPX_i18n_t("error.something-went-wrong", req->language));
			return;
		}
	}

	if (cHTTPX_JsonChild(message, "chat_ids", ids) != 0 || cHTTPX_JsonChild(root, "message", message) != 0)
	{
		*res = cHTTPX_ResError(cHTTPX_StatusInternalServerError, cHTTPX_i18n_t("error.something-went-wrong", req->language));
		return;
	}

	*res = cHTTPX_ResJsonObject(cHTTPX_StatusOK, root);
	return;

rollback:
    pg_res = db_exec(conn, "ROLLBACK");
    PQclear(pg_res);

    switch (r)
    {
    case DB_OK:
        break;

    case DB_TIMEOUT:
        *res = cHTTPX_ResError(cHTTPX_StatusConnectionTimedOut, cHTTPX_i18n_t("error.database-connection-timeout", req->language));
        return;

    case DB_DUPLICATE:
        *res = cHTTPX_ResError(cHTTPX_StatusBadRequest, cHTTPX_i18n_t("error.repeating-data-request", req->language));
        return;

    case DB_ERROR:
        *res = cHTTPX_ResError(cHTTPX_StatusInternalServerError, cHTTPX_i18n_t("error.perform-database-operation", req->language));
        return;
    }
}

void group_chats_delete_handler_v2(chttpx_request_t* req, chttpx_response_t* res)
{
    /* Context in request */
    auth_token_t* ctx = cHTTPX_ContextGet(req, AUTH_CONTEXT_NAME);
    if (!ctx || !ctx->user)
    {
        *res = cHTTPX_ResError(cHTTPX_StatusUnauthorized, cHTTPX_i18n_t("error.connect-to-account", req->language));
        return;
    }

    uint64_t group_id = 0;
    if (!cHTTPX_ParamU64(req, "group_id", &group_id))
    {
        *res = cHTTPX_ResError(cHTTPX_StatusBadRequest, cHTTPX_i18n_t("error.invalid-group-id", req->language));
        return;
    }

    db_result_t db_group_delete = db_group_chat_delete(http_server->conn, ctx->user->user_uid, group_id);

    switch (db_group_delete)
    {
    case DB_OK:
        break;

    case DB_TIMEOUT:
        *res = cHTTPX_ResError(cHTTPX_StatusConnectionTimedOut, cHTTPX_i18n_t("error.database-connection-timeout", req->language));
        return;

    case DB_DUPLICATE:
        *res = cHTTPX_ResError(cHTTPX_StatusBadRequest, cHTTPX_i18n_t("error.repeating-data-request", req->language));
        return;

    case DB_ERROR:
        *res = cHTTPX_ResError(cHTTPX_StatusInternalServerError, cHTTPX_i18n_t("error.perform-database-operation", req->language));
        return;
    }

    *res = cHTTPX_ResNoContent();
}
