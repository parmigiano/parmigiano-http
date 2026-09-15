#include "handlers.h"
#include "postgres/postgres_chats.h"

#include "s3.h"
#include "httpx.h"
#include "logger.h"
#include "utilities.h"

#include <string.h>
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

void chat_get_my_history_handler_v2(chttpx_request_t* req, chttpx_response_t* res)
{
    auth_token_t* ctx = (auth_token_t*)req->context;

    const char* offset_query = cHTTPX_Query(req, "offset");

    size_t offset = 0;
    if (offset_query && *offset_query != '\0')
        offset = strtoull(offset_query, NULL, 10);

    chat_preview_LIST_t* chats_preview = NULL;
    chats_preview = db_chat_get_my_history(http_server->conn, ctx->user->user_uid, offset);
    if (!chats_preview || chats_preview->count == 0)
    {
        *res = cHTTPX_ResJson(cHTTPX_StatusOK, "{\"message\": []}");
        goto cleanup;
    }

    size_t capacity = 8192;
    char* json = malloc(capacity);
    size_t len_json = 0;

    len_json += snprintf(json + len_json, capacity - len_json, "{\"message\": [");

    for (size_t i = 0; i < chats_preview->count; i++)
    {
        chat_preview_t* chat = &chats_preview->items[i];

        char item[2048];

        snprintf(item, sizeof(item),
                 "{"
                 "\"id\":%lu,"
                 "\"name\":\"%s\","
                 "\"username\":\"%s\","
                 "\"avatar\":\"%s\","
                 "\"user_uid\":%lu,"
                 "\"last_message\":\"%s\","
                 "\"last_message_date\":%ld,"
                 "\"unread_message_count\":%u"
                 "}%s",
                 chat->id, chat->name ? chat->name : "", chat->username ? chat->username : "", chat->avatar ? chat->avatar : "", chat->user_uid,
                 chat->last_message ? chat->last_message : "", chat->last_message_date, chat->unread_message_count,
                 (i + 1 < chats_preview->count) ? "," : "");

        size_t need_len = strlen(item);

        if (len_json + need_len + 1 > capacity)
        {
            capacity *= 2;
            json = realloc(json, capacity);
        }

        memcpy(json + len_json, item, need_len);
        len_json += need_len;
    }

    snprintf(json + len_json, capacity - len_json, "]}");

    *res = cHTTPX_ResJson(cHTTPX_StatusOK, "%s", json);

    free(json);

cleanup:
    if (chats_preview)
    {
        for (size_t i = 0; i < chats_preview->count; i++)
        {
            free(chats_preview->items[i].name);
            free(chats_preview->items[i].username);
            free(chats_preview->items[i].avatar);
            free(chats_preview->items[i].last_message);
        }

        free(chats_preview);
        chats_preview = NULL;
    }

    return;
}

void chat_get_by_username_handler_v2(chttpx_request_t* req, chttpx_response_t* res)
{
    auth_token_t* ctx = (auth_token_t*)req->context;

    const char* username_param = cHTTPX_Param(req, "username");

    chat_preview_LIST_t* chats_preview = NULL;
    chats_preview = db_chat_get_by_username(http_server->conn, ctx->user->user_uid, username_param);
    if (!chats_preview || chats_preview->count == 0)
    {
        *res = cHTTPX_ResJson(cHTTPX_StatusOK, "{\"message\": []}");
        goto cleanup;
    }

    size_t capacity = 8192;
    char* json = malloc(capacity);
    size_t len_json = 0;

    len_json += snprintf(json + len_json, capacity - len_json, "{\"message\": [");

    for (size_t i = 0; i < chats_preview->count; i++)
    {
        chat_preview_t* chat = &chats_preview->items[i];

        char item[2048];

        snprintf(item, sizeof(item),
                 "{"
                 "\"id\":%lu,"
                 "\"name\":\"%s\","
                 "\"username\":\"%s\","
                 "\"avatar\":\"%s\","
                 "\"user_uid\":%lu,"
                 "\"last_message\":\"%s\","
                 "\"last_message_date\":%ld,"
                 "\"unread_message_count\":%u"
                 "}%s",
                 chat->id, chat->name ? chat->name : "", chat->username ? chat->username : "", chat->avatar ? chat->avatar : "", chat->user_uid,
                 chat->last_message ? chat->last_message : "", chat->last_message_date, chat->unread_message_count,
                 (i + 1 < chats_preview->count) ? "," : "");

        size_t need_len = strlen(item);

        if (len_json + need_len + 1 > capacity)
        {
            capacity *= 2;
            json = realloc(json, capacity);
        }

        memcpy(json + len_json, item, need_len);
        len_json += need_len;
    }

    snprintf(json + len_json, capacity - len_json, "]}");

    *res = cHTTPX_ResJson(cHTTPX_StatusOK, "%s", json);

    free(json);

cleanup:
    if (chats_preview)
    {
        for (size_t i = 0; i < chats_preview->count; i++)
        {
            free(chats_preview->items[i].name);
            free(chats_preview->items[i].username);
            free(chats_preview->items[i].avatar);
            free(chats_preview->items[i].last_message);
        }

        free(chats_preview);
        chats_preview = NULL;
    }

    return;
}

void chat_get_settings_handler_v2(chttpx_request_t* req, chttpx_response_t* res)
{
    auth_token_t* ctx = (auth_token_t*)req->context;

    /* DB. get chat setting */
    chat_setting_t* chat_setting = NULL;

    const char* chat_id_param = cHTTPX_Param(req, "chat_id");

    uint64_t chat_id = 0;
    if (chat_id_param && *chat_id_param != '\0')
        chat_id = strtoull(chat_id_param, NULL, 10);

    bool is_member = db_chat_get_member_exists_by_chat_id(http_server->conn, chat_id, ctx->user->user_uid);
    if (!is_member)
    {
        *res = cHTTPX_ResJson(cHTTPX_StatusForbidden, "{\"error\": \"%s\"}", cHTTPX_i18n_t("error.not-member-chat", ctx->lang));
        goto cleanup;
    }

    chat_setting = db_chat_get_setting_by_chat_id(http_server->conn, chat_id);
    if (!chat_setting)
    {
        *res = cHTTPX_ResJson(cHTTPX_StatusNotFound, "{\"message\": \"NULL\"}");
        goto cleanup;
    }

    *res = cHTTPX_ResJson(cHTTPX_StatusOK,
                          "{"
                          "\"message\": {"
                          "\"id\": %lu,"
                          "\"created_at\": %ld,"
                          "\"updated_at\": %ld,"
                          "\"chat_id\": %lu,"
                          "\"custom_background\": \"%s\","
                          "\"blocked\": %s,"
                          "\"who_blocked_uid\": %lu"
                          "}"
                          "}",
                          chat_setting->id, chat_setting->created_at, chat_setting->updated_at, chat_setting->chat_id,
                          chat_setting->custom_background ? chat_setting->custom_background : "", chat_setting->blocked ? "true" : "false",
                          chat_setting->who_blocked_uid);

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
    auth_token_t* ctx = (auth_token_t*)req->context;

    /* db. get chat_setting */
    chat_setting_t* chat_setting = NULL;
    /* Initial URL avatar */
    char* url = NULL;

    const char* chat_id_param = cHTTPX_Param(req, "chat_id");

    uint64_t chat_id = 0;
    if (chat_id_param && *chat_id_param != '\0')
        chat_id = strtoull(chat_id_param, NULL, 10);

    bool is_member = db_chat_get_member_exists_by_chat_id(http_server->conn, chat_id, ctx->user->user_uid);
    if (!is_member)
    {
        *res = cHTTPX_ResJson(cHTTPX_StatusForbidden, "{\"error\": \"%s\"}", cHTTPX_i18n_t("error.not-member-chat", ctx->lang));
        goto cleanup;
    }

    if (req->filename[0] == '\0')
    {
        logger_error("chat_upload_custom_bg_handler_v2 req={%s}: empty media file", ctx->x_req_id);
        *res = cHTTPX_ResJson(cHTTPX_StatusBadRequest, "{\"error\": \"%s\"}", cHTTPX_i18n_t("error.no-data-to-process", ctx->lang));
        goto cleanup;
    }

    /* Jpeg/Jpg | Png */
    if (strcmp(req->content_type, cHTTPX_CTYPE_JPEG) != 0 && strcmp(req->content_type, cHTTPX_CTYPE_PNG) != 0)
    {
        logger_error("chat_upload_custom_bg_handler_v2 req={%s}: failed to load media forbidden file extension (%s)", ctx->x_req_id,
                     req->content_type);
        *res = cHTTPX_ResJson(cHTTPX_StatusBadRequest, "{\"error\": \"%s\"}", cHTTPX_i18n_t("error.forbidden-file-extension", ctx->lang));
        goto cleanup;
    }

    chat_setting = db_chat_get_setting_by_chat_id(http_server->conn, chat_id);
    if (!chat_setting)
    {
        *res = cHTTPX_ResJson(cHTTPX_StatusNotFound, "{\"error\": \"%s\"}", cHTTPX_i18n_t("error.chat-setting-not-found", ctx->lang));
        goto cleanup;
    }

    FILE* f = fopen(req->filename, "rb");
    if (!f)
    {
        logger_error("chat_upload_custom_bg_handler_v2 req={%s}: failed open temporary file", ctx->x_req_id);
        *res = cHTTPX_ResJson(cHTTPX_StatusInternalServerError, "{\"error\": \"%s\"}", cHTTPX_i18n_t("error.open-temporary-file", ctx->lang));
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
    snprintf(s3_bg_chat_key, sizeof(s3_bg_chat_key), "bg_chat_id_%ld", chat_id);

    url = s3_upload_file_pub(f, req->filename, req->content_type, s3_bg_chat_key, &s3_config);
    fclose(f);

    if (!url || *url == '\0')
    {
        logger_error("chat_upload_custom_bg_handler_v2 req={%s}: failed save file to S3 cloud", ctx->x_req_id);
        *res = cHTTPX_ResJson(cHTTPX_StatusInternalServerError, "{\"error\": \"%s\"}", cHTTPX_i18n_t("error.save-file", ctx->lang));
        goto cleanup;
    }

    /* Delete old custom bg in S3 */
    if (s3_delete_file(chat_setting->custom_background ? chat_setting->custom_background : "", &s3_config) != 0)
    {
        logger_error("chat_upload_custom_bg_handler_v2 req={%s}: failed to delete file in S3 cloud", ctx->x_req_id);
        *res = cHTTPX_ResJson(cHTTPX_StatusConflict, "{\"error\": \"%s\"}", cHTTPX_i18n_t("error.s3-cloud", ctx->lang));
        goto cleanup;
    }

    /* update cbackground in database */
    db_result_t chat_db_result = db_chat_upd_cbackground_by_chat_id(http_server->conn, url, chat_id);

    switch (chat_db_result)
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

    char* safe_url = escape_json_string(url);
    if (safe_url)
    {
        *res = cHTTPX_ResJson(cHTTPX_StatusOK, "{\"message\": \"%s\"}", safe_url);
        free(safe_url);
    }
    else
    {
        *res = cHTTPX_ResJson(cHTTPX_StatusInternalServerError, "{\"error\": \"%s\"}", cHTTPX_i18n_t("error.something-went-wrong", ctx->lang));
    }

cleanup:
    if (chat_setting)
    {
        free(chat_setting->custom_background);
        free(chat_setting);

        chat_setting = NULL;
    }

    if (url)
        free(url);

    if (req->filename[0] != '\0')
        remove(req->filename);

    return;
}

void chat_translate_handler_v2(chttpx_request_t* req, chttpx_response_t* res)
{
    /* Context in request */
    auth_token_t* ctx = (auth_token_t*)req->context;

    chat_translate_t payload = {0};

    chttpx_validation_t fields[] = {
        chttpx_validation_string("text", &payload.text, true, 0, 3000, VALIDATOR_NONE),
    };

    if (!cHTTPX_Parse(req, fields, (sizeof(fields) / sizeof(fields[0]))))
        goto errorjson;

    if (!cHTTPX_Validate(req, fields, (sizeof(fields) / sizeof(fields[0])), ctx->lang))
        goto errorjson;

    /* Safe message string */
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "text", payload.text);
    char* text_json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!text_json)
    {
        *res = cHTTPX_ResJson(cHTTPX_StatusInternalServerError, "{\"error\": \"%s\"}", cHTTPX_i18n_t("error.something-went-wrong", ctx->lang));
        goto cleanup;
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
    char* safe = escape_json_string(translated_text ? translated_text : "");
    if (safe)
    {
        *res = cHTTPX_ResJson(cHTTPX_StatusOK, "{\"message\": \"%s\"}", safe);
        free(safe);
    }
    else
    {
        *res = cHTTPX_ResJson(cHTTPX_StatusInternalServerError, "{\"error\": \"%s\"}", cHTTPX_i18n_t("error.something-went-wrong", ctx->lang));
    }

    free(translated_text);
    free(translated);
    free(text_json);

cleanup:
    /* Free payloads */
    free(payload.text);

    return;

errorjson:
    *res = cHTTPX_ResJson(cHTTPX_StatusBadRequest, "{\"error\": \"%s\"}", req->error_msg);

    goto cleanup;
}

void chat_bot_default_ai_handler_v2(chttpx_request_t* req, chttpx_response_t* res)
{
    /* Context in request */
    auth_token_t* ctx = (auth_token_t*)req->context;

    chat_bot_ai_t payload = {0};

    chttpx_validation_t fields[] = {
        chttpx_validation_string("message", &payload.message, true, 0, 3000, VALIDATOR_NONE),
    };

    if (!cHTTPX_Parse(req, fields, (sizeof(fields) / sizeof(fields[0]))))
        goto errorjson;

    if (!cHTTPX_Validate(req, fields, (sizeof(fields) / sizeof(fields[0])), ctx->lang))
        goto errorjson;

    /* Safe message string */
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "message", payload.message);
    char* message_json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!message_json)
    {
        *res = cHTTPX_ResJson(cHTTPX_StatusInternalServerError, "{\"error\": \"%s\"}", cHTTPX_i18n_t("error.something-went-wrong", ctx->lang));
        goto cleanup;
    }

    char* bot_ai_response = call_ai_text(message_json);
    free(message_json);

    if (bot_ai_response == NULL)
    {
        logger_error("chat_bot_default_ai_handler_v2 req={%s}: failed to get response by defailt AI BOT", ctx->x_req_id);

        *res = cHTTPX_ResJson(cHTTPX_StatusInternalServerError, "{\"error\": \"%s\"}", cHTTPX_i18n_t("error.service-temporarily-error", ctx->lang));
        goto cleanup;
    }

    char* ai_text = get_ollama_response(bot_ai_response);
    free(bot_ai_response);

    char* safe = escape_json_string(ai_text ? ai_text : "");
    free(ai_text);

    if (safe)
    {
        *res = cHTTPX_ResJson(cHTTPX_StatusOK, "{\"message\": \"%s\"}", safe);
        free(safe);
    }
    else
    {
        *res = cHTTPX_ResJson(cHTTPX_StatusInternalServerError, "{\"error\": \"%s\"}", cHTTPX_i18n_t("error.something-went-wrong", ctx->lang));
    }

cleanup:
    /* Free payloads */
    free(payload.message);

    return;

errorjson:
    *res = cHTTPX_ResJson(cHTTPX_StatusBadRequest, "{\"error\": \"%s\"}", req->error_msg);

    goto cleanup;
}