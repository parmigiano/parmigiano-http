#include "handlers.h"
#include "postgres/postgres_chats.h"

#include "s3.h"
#include "httpx.h"
#include "logger.h"
#include "utilities.h"

#include <string.h>
#include <inttypes.h>
#include <cjson/cJSON.h>
#include <libchttpx/libchttpx.h>

typedef struct
{
    char* text;
} chat_translate_t;

typedef struct
{
    char* message;
} chat_bot_ai_t;

static chttpx_response_t chat_preview_response(chttpx_request_t* req, const chat_preview_LIST_t* chats)
{
    chttpx_json_t* root = cHTTPX_JsonObject(req);
    chttpx_json_t* message = cHTTPX_JsonArray(req);

    if (!root || !message)
        return cHTTPX_ResError(cHTTPX_StatusInternalServerError, cHTTPX_i18n_t("error.something-went-wrong", req->language));

    if (chats)
    {
        for (size_t i = 0; i < chats->count; ++i)
        {
            const chat_preview_t* chat = &chats->items[i];
            chttpx_json_t* item = cHTTPX_JsonObject(req);

            if (!item || cHTTPX_JsonNumber(item, "id", (double)chat->id) != 0 ||
                cHTTPX_JsonString(item, "name", chat->name) != 0 ||
                cHTTPX_JsonString(item, "username", chat->username) != 0 ||
                cHTTPX_JsonString(item, "avatar", chat->avatar) != 0 ||
                cHTTPX_JsonNumber(item, "user_uid", (double)chat->user_uid) != 0 ||
                cHTTPX_JsonString(item, "last_message", chat->last_message) != 0 ||
                cHTTPX_JsonNumber(item, "last_message_date", (double)chat->last_message_date) != 0 ||
                cHTTPX_JsonNumber(item, "unread_message_count", chat->unread_message_count) != 0 ||
                cHTTPX_JsonArrayChild(message, item) != 0)
            {
                return cHTTPX_ResError(cHTTPX_StatusInternalServerError,
                                       cHTTPX_i18n_t("error.something-went-wrong", req->language));
            }
        }
    }

    if (cHTTPX_JsonChild(root, "message", message) != 0)
        return cHTTPX_ResError(cHTTPX_StatusInternalServerError, cHTTPX_i18n_t("error.something-went-wrong", req->language));

    return cHTTPX_ResJsonObject(cHTTPX_StatusOK, root);
}

static chttpx_response_t chat_setting_response(chttpx_request_t* req, const chat_setting_t* setting)
{
    chttpx_json_t* root = cHTTPX_JsonObject(req);
    chttpx_json_t* message = cHTTPX_JsonObject(req);

    if (!root || !message || cHTTPX_JsonNumber(message, "id", (double)setting->id) != 0 ||
        cHTTPX_JsonNumber(message, "created_at", (double)setting->created_at) != 0 ||
        cHTTPX_JsonNumber(message, "updated_at", (double)setting->updated_at) != 0 ||
        cHTTPX_JsonNumber(message, "chat_id", (double)setting->chat_id) != 0 ||
        cHTTPX_JsonString(message, "custom_background", setting->custom_background) != 0 ||
        cHTTPX_JsonBool(message, "blocked", setting->blocked) != 0 ||
        cHTTPX_JsonNumber(message, "who_blocked_uid", (double)setting->who_blocked_uid) != 0 ||
        cHTTPX_JsonChild(root, "message", message) != 0)
    {
        return cHTTPX_ResError(cHTTPX_StatusInternalServerError,
                               cHTTPX_i18n_t("error.something-went-wrong", req->language));
    }

    return cHTTPX_ResJsonObject(cHTTPX_StatusOK, root);
}

static void chat_preview_list_free(chat_preview_LIST_t* chats)
{
    if (!chats)
        return;

    for (size_t i = 0; i < chats->count; ++i)
    {
        free(chats->items[i].name);
        free(chats->items[i].username);
        free(chats->items[i].avatar);
        free(chats->items[i].last_message);
    }

    free(chats);
}

void chat_get_my_history_handler_v2(chttpx_request_t* req, chttpx_response_t* res)
{
    auth_token_t* ctx = cHTTPX_ContextGet(req, AUTH_CONTEXT_NAME);
    if (!ctx || !ctx->user)
    {
        *res = cHTTPX_ResError(cHTTPX_StatusUnauthorized, cHTTPX_i18n_t("error.connect-to-account", req->language));
        return;
    }

    uint64_t offset_value = 0;
    if (!cHTTPX_QueryU64Default(req, "offset", &offset_value, 0))
    {
        *res = cHTTPX_ResError(cHTTPX_StatusBadRequest, cHTTPX_i18n_t("error.invalid-offset", req->language));
        return;
    }
    size_t offset = (size_t)offset_value;

    chat_preview_LIST_t* chats_preview = db_chat_get_my_history(app_context->conn, ctx->user->user_uid, offset);
    *res = chat_preview_response(req, chats_preview);
    chat_preview_list_free(chats_preview);
}

void chat_get_by_username_handler_v2(chttpx_request_t* req, chttpx_response_t* res)
{
    auth_token_t* ctx = cHTTPX_ContextGet(req, AUTH_CONTEXT_NAME);
    if (!ctx || !ctx->user)
    {
        *res = cHTTPX_ResError(cHTTPX_StatusUnauthorized, cHTTPX_i18n_t("error.connect-to-account", req->language));
        return;
    }

    const char* username_param = cHTTPX_Param(req, "username");

    chat_preview_LIST_t* chats_preview = db_chat_get_by_username(app_context->conn, ctx->user->user_uid, username_param);
    *res = chat_preview_response(req, chats_preview);
    chat_preview_list_free(chats_preview);
}

void chat_get_settings_handler_v2(chttpx_request_t* req, chttpx_response_t* res)
{
    auth_token_t* ctx = cHTTPX_ContextGet(req, AUTH_CONTEXT_NAME);
    if (!ctx || !ctx->user)
    {
        *res = cHTTPX_ResError(cHTTPX_StatusUnauthorized, cHTTPX_i18n_t("error.connect-to-account", req->language));
        return;
    }

    /* DB. get chat setting */
    chat_setting_t* chat_setting = NULL;

    uint64_t chat_id = 0;
    if (!cHTTPX_ParamU64(req, "chat_id", &chat_id))
    {
        *res = cHTTPX_ResError(cHTTPX_StatusBadRequest, cHTTPX_i18n_t("error.invalid-chat-id", req->language));
        return;
    }

    bool is_member = db_chat_get_member_exists_by_chat_id(app_context->conn, chat_id, ctx->user->user_uid);
    if (!is_member)
    {
        *res = cHTTPX_ResError(cHTTPX_StatusForbidden, cHTTPX_i18n_t("error.not-member-chat", req->language));
        goto cleanup;
    }

    chat_setting = db_chat_get_setting_by_chat_id(app_context->conn, chat_id);
    if (!chat_setting)
    {
        *res = cHTTPX_ResError(cHTTPX_StatusNotFound, cHTTPX_i18n_t("error.chat-setting-not-found", req->language));
        goto cleanup;
    }

    *res = chat_setting_response(req, chat_setting);

cleanup:
    if (chat_setting)
    {
        free(chat_setting->custom_background);
        free(chat_setting);

        chat_setting = NULL;
    }

    return;
}

void chat_upload_custom_bg_handler_v2(chttpx_request_t* req, chttpx_response_t* res)
{
    auth_token_t* ctx = cHTTPX_ContextGet(req, AUTH_CONTEXT_NAME);
    if (!ctx || !ctx->user)
    {
        *res = cHTTPX_ResError(cHTTPX_StatusUnauthorized, cHTTPX_i18n_t("error.connect-to-account", req->language));
        return;
    }

    /* db. get chat_setting */
    chat_setting_t* chat_setting = NULL;
    /* Initial URL avatar */
    char* url = NULL;
    const chttpx_file_t* file = cHTTPX_RequestFile(req);

    uint64_t chat_id = 0;
    if (!cHTTPX_ParamU64(req, "chat_id", &chat_id))
    {
        *res = cHTTPX_ResError(cHTTPX_StatusBadRequest, cHTTPX_i18n_t("error.invalid-chat-id", req->language));
        return;
    }

    bool is_member = db_chat_get_member_exists_by_chat_id(app_context->conn, chat_id, ctx->user->user_uid);
    if (!is_member)
    {
        *res = cHTTPX_ResError(cHTTPX_StatusForbidden, cHTTPX_i18n_t("error.not-member-chat", req->language));
        goto cleanup;
    }

    if (!file)
    {
        logger_error("chat_upload_custom_bg_handler_v2 req={%s}: empty media file", req->request_id);
        *res = cHTTPX_ResError(cHTTPX_StatusBadRequest, cHTTPX_i18n_t("error.no-data-to-process", req->language));
        goto cleanup;
    }

    /* Jpeg/Jpg | Png */
    if (!cHTTPX_MimeMatch(file->content_type, cHTTPX_CTYPE_JPEG) && !cHTTPX_MimeMatch(file->content_type, cHTTPX_CTYPE_PNG))
    {
        logger_error("chat_upload_custom_bg_handler_v2 req={%s}: failed to load media forbidden file extension (%s)", req->request_id,
                     file->content_type);
        *res = cHTTPX_ResError(cHTTPX_StatusBadRequest, cHTTPX_i18n_t("error.forbidden-file-extension", req->language));
        goto cleanup;
    }

    chat_setting = db_chat_get_setting_by_chat_id(app_context->conn, chat_id);
    if (!chat_setting)
    {
        *res = cHTTPX_ResError(cHTTPX_StatusNotFound, cHTTPX_i18n_t("error.chat-setting-not-found", req->language));
        goto cleanup;
    }

    FILE* f = fopen(file->path, "rb");
    if (!f)
    {
        logger_error("chat_upload_custom_bg_handler_v2 req={%s}: failed open temporary file", req->request_id);
        *res = cHTTPX_ResError(cHTTPX_StatusInternalServerError, cHTTPX_i18n_t("error.open-temporary-file", req->language));
        goto cleanup;
    }

    s3_config_t s3_config = {
        .endpoint = getenv("S3_ENDPOINT"),
        .bucket = getenv("S3_BUCKET_PUB"),
        .access_key = getenv("S3_ACCESS_KEY"),
        .secret_key = getenv("S3_SECRET_KEY"),
        .region = getenv("S3_REGION"),
    };

    char s3_bg_chat_key[256];
    snprintf(s3_bg_chat_key, sizeof(s3_bg_chat_key), "bg_chat_id_%" PRIu64, chat_id);

    url = s3_upload_file_pub(f, file->path, file->content_type, s3_bg_chat_key, &s3_config);
    fclose(f);

    if (!url || *url == '\0')
    {
        logger_error("chat_upload_custom_bg_handler_v2 req={%s}: failed save file to S3 cloud", req->request_id);
        *res = cHTTPX_ResError(cHTTPX_StatusInternalServerError, cHTTPX_i18n_t("error.save-file", req->language));
        goto cleanup;
    }

    /* Delete old custom bg in S3 */
    if (s3_delete_file(chat_setting->custom_background ? chat_setting->custom_background : "", &s3_config) != 0)
    {
        logger_error("chat_upload_custom_bg_handler_v2 req={%s}: failed to delete file in S3 cloud", req->request_id);
        *res = cHTTPX_ResError(cHTTPX_StatusConflict, cHTTPX_i18n_t("error.s3-cloud", req->language));
        goto cleanup;
    }

    /* update cbackground in database */
    db_result_t chat_db_result = db_chat_upd_cbackground_by_chat_id(app_context->conn, url, chat_id);

    switch (chat_db_result)
    {
    case DB_OK:
        break;

    case DB_TIMEOUT:
        *res = cHTTPX_ResError(cHTTPX_StatusConnectionTimedOut, cHTTPX_i18n_t("error.database-connection-timeout", req->language));
        goto cleanup;

    case DB_DUPLICATE:
        *res = cHTTPX_ResError(cHTTPX_StatusBadRequest, cHTTPX_i18n_t("error.repeating-data-request", req->language));
        goto cleanup;

    case DB_ERROR:
        *res = cHTTPX_ResError(cHTTPX_StatusInternalServerError, cHTTPX_i18n_t("error.perform-database-operation", req->language));
        goto cleanup;
    }

    *res = cHTTPX_ResMessage(cHTTPX_StatusOK, url);

cleanup:
    if (chat_setting)
    {
        free(chat_setting->custom_background);
        free(chat_setting);

        chat_setting = NULL;
    }

    if (url)
        free(url);

    return;
}

void chat_translate_handler_v2(chttpx_request_t* req, chttpx_response_t* res)
{
    auth_token_t* ctx = cHTTPX_ContextGet(req, AUTH_CONTEXT_NAME);
    if (!ctx || !ctx->user)
    {
        *res = cHTTPX_ResError(cHTTPX_StatusUnauthorized, cHTTPX_i18n_t("error.connect-to-account", req->language));
        return;
    }

    chat_translate_t payload = {0};

    chttpx_validation_t fields[] = {
        cHTTPX_StringField("text", &payload.text, true, 0, 3000, CHTTPX_NORMALIZE_NONE, NULL),
    };

    if (!bind_json_i18n(req, res, fields, CHTTPX_ARRAY_LEN(fields)))
        return;

    /* Safe message string */
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "text", payload.text);
    char* text_json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!text_json)
    {
        *res = cHTTPX_ResError(cHTTPX_StatusInternalServerError, cHTTPX_i18n_t("error.something-went-wrong", req->language));
        return;
    }

    /* Detect language text */
    char* lang = detect_lang(text_json);
    if (!lang)
        lang = strdup("en");

    char* translated = NULL;

    /* Translate text with lang */
    if (strcmp(lang, "ru") == 0)
    {
        translated = translate(text_json, "ru", "en");
    }
    else if (strcmp(lang, "en") == 0)
    {
        translated = translate(text_json, "en", "ru");
    }
    else
    {
        translated = strdup(text_json);
    }

    if (translated == NULL)
        translated = strdup(text_json);

    free(lang);

    char* translated_text = get_translated_text(translated);
    *res = cHTTPX_ResMessage(cHTTPX_StatusOK, translated_text ? translated_text : "");

    free(translated_text);
    free(translated);
    free(text_json);
}

void chat_bot_default_ai_handler_v2(chttpx_request_t* req, chttpx_response_t* res)
{
    auth_token_t* ctx = cHTTPX_ContextGet(req, AUTH_CONTEXT_NAME);
    if (!ctx || !ctx->user)
    {
        *res = cHTTPX_ResError(cHTTPX_StatusUnauthorized, cHTTPX_i18n_t("error.connect-to-account", req->language));
        return;
    }

    chat_bot_ai_t payload = {0};

    chttpx_validation_t fields[] = {
        cHTTPX_StringField("message", &payload.message, true, 0, 3000, CHTTPX_NORMALIZE_NONE, NULL),
    };

    if (!bind_json_i18n(req, res, fields, CHTTPX_ARRAY_LEN(fields)))
        return;

    /* Safe message string */
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "message", payload.message);
    char* message_json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!message_json)
    {
        *res = cHTTPX_ResError(cHTTPX_StatusInternalServerError, cHTTPX_i18n_t("error.something-went-wrong", req->language));
        return;
    }

    char* bot_ai_response = call_ai_text(message_json);
    free(message_json);

    if (bot_ai_response == NULL)
    {
        logger_error("chat_bot_default_ai_handler_v2 req={%s}: failed to get response by defailt AI BOT", req->request_id);

        *res = cHTTPX_ResError(cHTTPX_StatusInternalServerError, cHTTPX_i18n_t("error.service-temporarily-error", req->language));
        return;
    }

    char* ai_text = get_ollama_response(bot_ai_response);
    free(bot_ai_response);

    *res = cHTTPX_ResMessage(cHTTPX_StatusOK, ai_text ? ai_text : "");
    free(ai_text);
}
