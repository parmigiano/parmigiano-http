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
#include <inttypes.h>

void media_upload_in_chat_handler_v2(chttpx_request_t* req, chttpx_response_t* res)
{
    auth_token_t* ctx = cHTTPX_ContextGet(req, AUTH_CONTEXT_NAME);
    if (!ctx || !ctx->user)
    {
        *res = cHTTPX_ResError(cHTTPX_StatusUnauthorized, cHTTPX_i18n_t("error.connect-to-account", req->language));
        return;
    }

    /* Initial URL avatar */
    char* s3_key = NULL;
    const chttpx_file_t* file = cHTTPX_RequestFile(req);

    uint64_t chat_id = 0;
    if (!cHTTPX_ParamU64(req, "chat_id", &chat_id))
    {
        *res = cHTTPX_ResError(cHTTPX_StatusBadRequest, cHTTPX_i18n_t("error.invalid-chat-id", req->language));
        return;
    }

    bool is_member = db_chat_get_member_exists_by_chat_id(http_server->conn, chat_id, ctx->user->user_uid);
    if (!is_member)
    {
        *res = cHTTPX_ResError(cHTTPX_StatusForbidden, cHTTPX_i18n_t("error.not-member-chat", req->language));
        goto cleanup;
    }

    if (!file)
    {
        logger_error("media_upload_in_chat_handler_v2 req={%s}: empty media file", req->request_id);
        *res = cHTTPX_ResError(cHTTPX_StatusBadRequest, cHTTPX_i18n_t("error.no-data-to-process", req->language));
        goto cleanup;
    }

    FILE* f = fopen(file->path, "rb");
    if (!f)
    {
        logger_error("media_upload_in_chat_handler_v2 req={%s}: failed open temporary file", req->request_id);
        *res = cHTTPX_ResError(cHTTPX_StatusInternalServerError, cHTTPX_i18n_t("error.open-temporary-file", req->language));
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
    snprintf(s3_chat_media_key, sizeof(s3_chat_media_key), "chats/%" PRIu64, chat_id);

    /* save to s3 storage */
    s3_key = s3_upload_file_prv(f, file->path, file->content_type, s3_chat_media_key, &s3_config);
    fclose(f);

    if (!s3_key || *s3_key == '\0')
    {
        *res = cHTTPX_ResError(cHTTPX_StatusInternalServerError, cHTTPX_i18n_t("error.save-file", req->language));
        goto cleanup;
    }

    const char* mime_file_type = map_mime_to_msg_type(file->content_type);

    /* calloc message_t */
    message_t* message = calloc(1, sizeof(message_t));
    if (!message)
    {
        logger_error("media_upload_in_chat_handler_v2 req={%s}: calloc failed for message_t", req->request_id);

        fprintf(stderr, "calloc failed\n");
        *res = cHTTPX_ResError(cHTTPX_StatusInternalServerError, cHTTPX_i18n_t("error.something-went-wrong", req->language));

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

        *res = cHTTPX_ResError(cHTTPX_StatusInternalServerError, cHTTPX_i18n_t("error.something-went-wrong", req->language));
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

    *res = cHTTPX_ResNoContent();

cleanup:
    if (s3_key)
        free(s3_key);

    return;
}
