#include "handlers.h"

#include "s3.h"
#include "httpx.h"
#include "logger.h"
#include "nsfw.h"
#include "utilities.h"

#include <inttypes.h>

void user_me_handler_v2(chttpx_request_t* req, chttpx_response_t* res)
{
    auth_token_t* ctx = cHTTPX_ContextGet(req, AUTH_CONTEXT_NAME);
	if (!ctx || !ctx->user)
    {
        *res = cHTTPX_ResError(cHTTPX_StatusUnauthorized, cHTTPX_i18n_t("error.connect-to-account", req->language));
        return;
    }

    chttpx_json_t* root = cHTTPX_JsonObject(req);
    chttpx_json_t* message = cHTTPX_JsonObject(req);

    if (!root || !message || cHTTPX_JsonNumber(message, "id", (double)ctx->user->id) != 0 ||
        cHTTPX_JsonNumber(message, "created_at", (double)ctx->user->created_at) != 0 ||
        cHTTPX_JsonNumber(message, "user_uid", (double)ctx->user->user_uid) != 0 ||
        cHTTPX_JsonString(message, "avatar", ctx->user->avatar) != 0 ||
        cHTTPX_JsonString(message, "name", ctx->user->name) != 0 ||
        cHTTPX_JsonString(message, "username", ctx->user->username) != 0 ||
        cHTTPX_JsonBool(message, "username_visible", ctx->user->username_visible) != 0 ||
        cHTTPX_JsonString(message, "email", ctx->user->email) != 0 ||
        cHTTPX_JsonBool(message, "email_visible", ctx->user->email_visible) != 0 ||
        cHTTPX_JsonBool(message, "email_confirm", ctx->user->email_confirm) != 0 ||
        cHTTPX_JsonString(message, "phone", ctx->user->phone) != 0 ||
        cHTTPX_JsonBool(message, "phone_visible", ctx->user->phone_visible) != 0 ||
        cHTTPX_JsonString(message, "overview", ctx->user->overview) != 0 ||
        cHTTPX_JsonChild(root, "message", message) != 0)
    {
        *res = cHTTPX_ResError(cHTTPX_StatusInternalServerError, cHTTPX_i18n_t("error.something-went-wrong", req->language));
        return;
    }

    *res = cHTTPX_ResJsonObject(cHTTPX_StatusOK, root);
}

void user_upload_avatar_handler_v2(chttpx_request_t* req, chttpx_response_t* res)
{
    auth_token_t* ctx = cHTTPX_ContextGet(req, AUTH_CONTEXT_NAME);
	if (!ctx || !ctx->user)
    {
        *res = cHTTPX_ResError(cHTTPX_StatusUnauthorized, cHTTPX_i18n_t("error.connect-to-account", req->language));
        return;
    }

    char* url = NULL;
    bool remove_uploaded_file = false;
    s3_config_t s3_config = {0};

    /* Prefer multipart/form-data field `avatar`, keep raw upload compatibility. */
    const chttpx_file_t* file = cHTTPX_FormFile(req, "avatar");
    if (!file)
        file = cHTTPX_RequestFile(req);

    if (!file || !file->path || file->size == 0)
    {
        *res = cHTTPX_ResError(cHTTPX_StatusBadRequest, cHTTPX_i18n_t("error.no-data-to-process", req->language));
        return;
    }

    /* Jpeg/Jpg | Png | Gif */
    if (!cHTTPX_MimeIsImage(file->content_type))
    {
        *res = cHTTPX_ResError(cHTTPX_StatusBadRequest, cHTTPX_i18n_t("error.forbidden-file-extension", req->language));
        return;
    }

    /* Moderate before publishing the avatar to S3 or updating the profile. */
    nsfw_error_t nsfw_error;
    nsfw_result_t nsfw_result = nsfw_check_file_from_env(file->path, file->content_type, &nsfw_error);

    if (nsfw_result != NSFW_OK)
    {
        uint16_t status = cHTTPX_StatusServiceUnavailable;

        switch (nsfw_result)
        {
        case NSFW_REJECTED:
            status = cHTTPX_StatusUnprocessableEntity;
            break;
        case NSFW_EMPTY_IMAGE:
        case NSFW_INVALID_IMAGE:
            status = cHTTPX_StatusBadRequest;
            break;
        case NSFW_IMAGE_TOO_LARGE:
            status = cHTTPX_StatusPayloadTooLarge;
            break;
        case NSFW_UNSUPPORTED_MEDIA:
            status = cHTTPX_StatusUnsupportedMediaType;
            break;
        case NSFW_TIMEOUT:
            status = cHTTPX_StatusGatewayTimeout;
            break;
        case NSFW_INVALID_ARGUMENT:
        case NSFW_INVALID_URL:
        case NSFW_FILE_ERROR:
        case NSFW_OUT_OF_MEMORY:
        case NSFW_CLIENT_ERROR:
            status = cHTTPX_StatusInternalServerError;
            break;
        case NSFW_INVALID_RESPONSE:
        case NSFW_RESPONSE_TOO_LARGE:
        case NSFW_SERVICE_ERROR:
        case NSFW_HTTP_ERROR:
            status = cHTTPX_StatusBadGateway;
            break;
        default:
            break;
        }

        if (nsfw_result != NSFW_REJECTED)
        {
            logger_error("user_upload_avatar_handler_v2 req={%s}: NSFW check failed: %s", req->request_id ? req->request_id : "", nsfw_error.detail);
        }

        *res = cHTTPX_ResError(status, cHTTPX_i18n_t(nsfw_result_locale_key(nsfw_result), req->language));
        return;
    }

    FILE* f = fopen(file->path, "rb");
    if (!f)
    {
        *res = cHTTPX_ResError(cHTTPX_StatusInternalServerError, cHTTPX_i18n_t("error.open-temporary-file", req->language));
        return;
    }

    s3_config = (s3_config_t){
        .endpoint = getenv("S3_ENDPOINT"),
        .bucket = getenv("S3_BUCKET_PUB"),
        .access_key = getenv("S3_ACCESS_KEY"),
        .secret_key = getenv("S3_SECRET_KEY"),
        .region = getenv("S3_REGION"),
    };

    /* key for s3 storage */
    char s3_avatar_key[128];
    snprintf(s3_avatar_key, sizeof(s3_avatar_key), "avatar_user_uid_%" PRIu64, ctx->user->user_uid);

    /* file->path is a temporary file without an extension; preserve the original form filename instead. */
    url = s3_upload_file_pub(f, file->original_name, file->content_type, s3_avatar_key, &s3_config);
    fclose(f);

    if (!url || *url == '\0')
    {
        *res = cHTTPX_ResError(cHTTPX_StatusInternalServerError, cHTTPX_i18n_t("error.save-file", req->language));
        goto cleanup;
    }
    remove_uploaded_file = true;

    /* update in database */
    db_result_t user_upd_result = db_user_profile_upd_avatar_by_uid(http_server->conn, ctx->user->user_uid, url);

    switch (user_upd_result)
    {
    case DB_OK:
        remove_uploaded_file = false;
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

    /* The profile already points to the new avatar, so failure to delete the old object is non-fatal. */
    if (ctx->user->avatar && *ctx->user->avatar && strcmp(ctx->user->avatar, url) != 0)
    {
        if (s3_delete_file(ctx->user->avatar, &s3_config) != 0)
            logger_warn("user_upload_avatar_handler_v2 req={%s}: failed to delete previous avatar", req->request_id);
    }

    *res = cHTTPX_ResMessage(cHTTPX_StatusOK, url);

cleanup:
    if (remove_uploaded_file && url)
    {
        if (s3_delete_file(url, &s3_config) != 0)
            logger_error("user_upload_avatar_handler_v2 req={%s}: failed to rollback S3 avatar upload", req->request_id);
    }

    free(url);
}

void user_get_profile_handler_v2(chttpx_request_t* req, chttpx_response_t* res)
{
    auth_token_t* ctx = cHTTPX_ContextGet(req, AUTH_CONTEXT_NAME);
    if (!ctx || !ctx->user)
    {
        *res = cHTTPX_ResError(cHTTPX_StatusUnauthorized, cHTTPX_i18n_t("error.connect-to-account", req->language));
        return;
    }

    /* DB. get user info */
    user_info_t* user = NULL;

    uint64_t user_uid = 0;
    if (!cHTTPX_ParamU64(req, "user_uid", &user_uid))
    {
        *res = cHTTPX_ResError(cHTTPX_StatusBadRequest, cHTTPX_i18n_t("error.user-not-found", req->language));
        goto cleanup;
    }

    user = db_user_info_get_by_uid(http_server->conn, user_uid);
    if (!user)
    {
        *res = cHTTPX_ResError(cHTTPX_StatusNotFound, cHTTPX_i18n_t("error.user-not-found", req->language));
        goto cleanup;
    }

    chttpx_json_t* root = cHTTPX_JsonObject(req);
    chttpx_json_t* message = cHTTPX_JsonObject(req);

    if (!root || !message || cHTTPX_JsonNumber(message, "id", (double)user->id) != 0 ||
        cHTTPX_JsonNumber(message, "created_at", (double)user->created_at) != 0 ||
        cHTTPX_JsonNumber(message, "user_uid", (double)user->user_uid) != 0 ||
        cHTTPX_JsonString(message, "avatar", user->avatar) != 0 ||
        cHTTPX_JsonString(message, "name", user->name) != 0 ||
        cHTTPX_JsonString(message, "username", user->username) != 0 ||
        cHTTPX_JsonBool(message, "username_visible", user->username_visible) != 0 ||
        cHTTPX_JsonString(message, "email", user->email) != 0 ||
        cHTTPX_JsonBool(message, "email_visible", user->email_visible) != 0 ||
        cHTTPX_JsonBool(message, "email_confirm", user->email_confirm) != 0 ||
        cHTTPX_JsonString(message, "phone", user->phone) != 0 ||
        cHTTPX_JsonBool(message, "phone_visible", user->phone_visible) != 0 ||
        cHTTPX_JsonString(message, "overview", user->overview) != 0 ||
        cHTTPX_JsonChild(root, "message", message) != 0)
    {
        *res = cHTTPX_ResError(cHTTPX_StatusInternalServerError, cHTTPX_i18n_t("error.something-went-wrong", req->language));
        goto cleanup;
    }

    *res = cHTTPX_ResJsonObject(cHTTPX_StatusOK, root);

cleanup:
    if (user)
    {
        db_user_info_free(user);
        user = NULL;
    }

    return;
}

void user_update_profile_handler_v2(chttpx_request_t* req, chttpx_response_t* res)
{
    auth_token_t* ctx = cHTTPX_ContextGet(req, AUTH_CONTEXT_NAME);
    if (!ctx || !ctx->user)
    {
        *res = cHTTPX_ResError(cHTTPX_StatusUnauthorized, cHTTPX_i18n_t("error.connect-to-account", req->language));
        return;
    }

    user_profile_update_t payload = {0};
    bool username_visible = false;
    bool email_visible = false;
    bool phone_visible = false;

    chttpx_validation_t fields[] = {
        cHTTPX_StringField("username", &payload.username, false, 4, 24, CHTTPX_TRIM, validate_username),
        chttpx_validation_boolean("username_visible", &username_visible, false),
        cHTTPX_StringField("name", &payload.name, false, 0, 96, CHTTPX_TRIM, validate_name),
        cHTTPX_StringField("email", &payload.email, false, 5, 254, CHTTPX_TRIM | CHTTPX_LOWERCASE, validate_email),
        chttpx_validation_boolean("email_visible", &email_visible, false),
        cHTTPX_StringField("phone", &payload.phone, false, 8, 254, CHTTPX_TRIM, NULL),
        chttpx_validation_boolean("phone_visible", &phone_visible, false),
        cHTTPX_StringField("overview", &payload.overview, false, 1, 125, CHTTPX_TRIM, NULL),
        cHTTPX_StringField("password", &payload.password, false, 8, 16, CHTTPX_NORMALIZE_NONE, validate_password),
    };

    if (!bind_json_i18n(req, res, fields, CHTTPX_ARRAY_LEN(fields)))
        return;

    payload.username_visible = fields[1].present ? &username_visible : NULL;
    payload.email_visible = fields[4].present ? &email_visible : NULL;
    payload.phone_visible = fields[6].present ? &phone_visible : NULL;

    char* password_hash = NULL;

    if (payload.password)
    {
        password_hash = hash_password(payload.password);
        if (!password_hash)
        {
            *res = cHTTPX_ResError(cHTTPX_StatusInternalServerError, cHTTPX_i18n_t("error.password-hash-failed", req->language));
            goto cleanup;
        }

        payload.password = password_hash;
    }

    db_result_t user_db_result = db_user_UPDATE_upd(http_server->conn, ctx->user->user_uid, &payload);

    switch (user_db_result)
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

    *res = cHTTPX_ResMessage(cHTTPX_StatusOK, cHTTPX_i18n_t("profile-updated", req->language));

cleanup:
    free(password_hash);

    return;

}
