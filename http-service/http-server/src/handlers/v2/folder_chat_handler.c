#include "handlers.h"

#include "httpx.h"
#include "postgres/postgres.h"
#include "postgres/postgres_group_chats.h"
#include "postgres/validate.h"

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

typedef struct {
    uint64_t user_uid;
    char* name;
    int* chat_ids;
    size_t chat_count;
    uint64_t group_id;
} group_chat_tx_t;

static db_result_t group_chat_create_tx(db_t* db, void* arg)
{
    group_chat_tx_t* tx = arg;
    chat_group_t group = {.user_uid = tx->user_uid, .name = tx->name};

    db_result_t result = db_group_chat_create(db, &group, &tx->group_id);
    if (result != DB_OK)
        return result;
    if (tx->group_id == 0)
        return DB_ERROR;

    return db_group_chat_add_chats(db, tx->user_uid, tx->group_id, tx->chat_ids, tx->chat_count);
}

static db_result_t group_chat_edit_tx(db_t* db, void* arg)
{
    group_chat_tx_t* tx = arg;

    db_result_t result = db_group_chat_edit_name_by_group_id(db, tx->group_id, tx->user_uid, tx->name);
    if (result != DB_OK)
        return result;

    return db_group_chat_edit_chats_by_group_id(db, tx->group_id, tx->user_uid, tx->chat_ids, tx->chat_count);
}

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
    group_chat_tx_t tx = {
        .user_uid = ctx->user->user_uid,
        .name = payload.name,
        .chat_ids = payload.chat_ids.items,
        .chat_count = payload.chat_ids.count,
        .group_id = 0,
    };

    db_result_t r = db_transaction(app_context->db, group_chat_create_tx, &tx);
    if (db_reply_result(req, res, r))
        return;

    uint64_t group_id = tx.group_id;

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

    group_chat_tx_t tx = {
        .user_uid = ctx->user->user_uid,
        .name = payload.name,
        .chat_ids = payload.chat_ids.items,
        .chat_count = payload.chat_ids.count,
        .group_id = group_id,
    };

    db_result_t r = db_transaction(app_context->db, group_chat_edit_tx, &tx);
    if (db_reply_result(req, res, r))
        return;

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

    db_result_t db_group_delete = db_group_chat_delete(app_context->db, ctx->user->user_uid, group_id);
    if (db_reply_result(req, res, db_group_delete))
        return;

    *res = cHTTPX_ResNoContent();
}
