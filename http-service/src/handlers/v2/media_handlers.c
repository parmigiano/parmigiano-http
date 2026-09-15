#include "handlers.h"

#include "s3.h"
#include "httpx.h"
#include "logger.h"
#include "utilities.h"

#include "postgres/postgres_chats.h"
#include "postgres/postgres_messages.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void media_upload_in_chat_handler_v2(chttpx_request_t* req, chttpx_response_t* res)
{
    auth_token_t* ctx = (auth_token_t*)req->context;

    /* Initial URL avatar */
    char* s3_key = NULL;

    /* Get from params -> chat_id */
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
        logger_error("media_upload_in_chat_handler_v2 req={%s}: empty media file", ctx->x_req_id);
        *res = cHTTPX_ResJson(cHTTPX_StatusBadRequest, "{\"error\": \"%s\"}", cHTTPX_i18n_t("error.no-data-to-process", ctx->lang));
        goto cleanup;
    }

    FILE* f = fopen(req->filename, "rb");
    if (!f)
    {
        logger_error("media_upload_in_chat_handler_v2 req={%s}: failed open temporary file", ctx->x_req_id);
        *res = cHTTPX_ResJson(cHTTPX_StatusInternalServerError, "{\"error\": \"%s\"}", cHTTPX_i18n_t("error.open-temporary-file", ctx->lang));
        goto cleanup;
    }

    s3_config_t s3_config = {
        .endpoint = getenv("S3_ENDPOINT"),
        .bucket = getenv("S3_BUCKET_PRV"),
        .access_key = getenv("S3_ACCESS_KEY"),
        .secret_key = getenv("S3_SECRET_KEY"),
        .region = getenv("S3_REGION"),
    };

    /* key for s3 storage */
    char s3_chat_media_key[256];
    snprintf(s3_chat_media_key, sizeof(s3_chat_media_key), "chats/%ld", chat_id);

    /* save to s3 storage */
    s3_key = s3_upload_file_prv(f, req->filename, req->content_type, s3_chat_media_key, &s3_config);
    fclose(f);

    if (!s3_key || *s3_key == '\0')
    {
        *res = cHTTPX_ResJson(cHTTPX_StatusInternalServerError, "{\"error\": \"%s\"}", cHTTPX_i18n_t("error.save-file", ctx->lang));
        goto cleanup;
    }

    const char* mime_file_type = map_mime_to_msg_type(req->content_type);

    /* calloc message_t */
    message_t* message = calloc(1, sizeof(message_t));
    if (!message)
    {
        logger_error("media_upload_in_chat_handler_v2 req={%s}: calloc failed for message_t", ctx->x_req_id);

        fprintf(stderr, "calloc failed\n");
        *res = cHTTPX_ResJson(cHTTPX_StatusInternalServerError, "{\"error\": \"%s\"}", cHTTPX_i18n_t("error.something-went-wrong", ctx->lang));

        goto cleanup;
    }
    message->chat_id = chat_id;
    message->sender_uid = ctx->user->user_uid;
    message->content = strdup(s3_key);
    message->content_type = strdup(mime_file_type);

    if (!message->content || !message->content_type)
    {
        free(message->content);
        free(message->content_type);
        free(message);
        
        *res = cHTTPX_ResJson(cHTTPX_StatusInternalServerError, "{\"error\": \"%s\"}", cHTTPX_i18n_t("error.something-went-wrong", ctx->lang));
        goto cleanup;
    }

    /* update in database */
    db_result_t message_db_result = db_message_create_all(http_server->conn, message);

    /* free memory */
    free(message->content);
    free(message->content_type);
    free(message);
    message = NULL;

    switch (message_db_result)
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
    if (s3_key)
        free(s3_key);

    if (req->filename[0] != '\0')
        remove(req->filename);

    return;
}