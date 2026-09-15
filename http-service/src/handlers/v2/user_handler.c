#include "handlers.h"

#include "s3.h"
#include "httpx.h"
#include "utilities.h"

void user_me_handler_v2(chttpx_request_t* req, chttpx_response_t* res)
{
    auth_token_t* ctx = (auth_token_t*)req->context;

    if (!ctx->user)
    {
        *res = cHTTPX_ResJson(cHTTPX_StatusNotFound, "{\"error\": \"%s\"}", cHTTPX_i18n_t("error.user-not-found", ctx->lang));
        goto cleanup;
    }

    *res = cHTTPX_ResJson(
        cHTTPX_StatusOK,
        "{"
        "\"message\": {"
        "\"id\": %lu,"
        "\"created_at\": %ld,"
        "\"user_uid\": %lu,"
        "\"avatar\": \"%s\","
        "\"name\": \"%s\","
        "\"username\": \"%s\","
        "\"username_visible\": %s,"
        "\"email\": \"%s\","
        "\"email_visible\": %s,"
        "\"email_confirm\": %s,"
        "\"phone\": \"%s\","
        "\"phone_visible\": %s,"
        "\"overview\": \"%s\""
        "}"
        "}",
        ctx->user->id, ctx->user->created_at, ctx->user->user_uid, ctx->user->avatar ? ctx->user->avatar : "", ctx->user->name ? ctx->user->name : "",
        ctx->user->username ? ctx->user->username : "", ctx->user->username_visible ? "true" : "false", ctx->user->email ? ctx->user->email : "",
        ctx->user->email_visible ? "true" : "false", ctx->user->email_confirm ? "true" : "false", ctx->user->phone ? ctx->user->phone : "",
        ctx->user->phone_visible ? "true" : "false", ctx->user->overview ? ctx->user->overview : "");

cleanup:

    return;
}

void user_upload_avatar_handler_v2(chttpx_request_t* req, chttpx_response_t* res)
{
    auth_token_t* ctx = (auth_token_t*)req->context;

    /* Initial URL avatar */
    char* url = NULL;

    if (req->filename[0] == '\0')
    {
        *res = cHTTPX_ResJson(cHTTPX_StatusBadRequest, "{\"error\": \"%s\"}", cHTTPX_i18n_t("error.no-data-to-process", ctx->lang));
        goto cleanup;
    }

    /* Jpeg/Jpg | Png | Gif */
    if (strcmp(req->content_type, cHTTPX_CTYPE_JPEG) != 0 && strcmp(req->content_type, cHTTPX_CTYPE_PNG) != 0 &&
        strcmp(req->content_type, cHTTPX_CTYPE_GIF) != 0)
    {
        *res = cHTTPX_ResJson(cHTTPX_StatusBadRequest, "{\"error\": \"%s\"}", cHTTPX_i18n_t("error.forbidden-file-extension", ctx->lang));
        goto cleanup;
    }

    FILE* f = fopen(req->filename, "rb");
    if (!f)
    {
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

    /* key for s3 storage */
    char s3_avatar_key[128];
    snprintf(s3_avatar_key, sizeof(s3_avatar_key), "avatar_user_uid_%ld", ctx->user->user_uid);

    /* save to s3 storage */
    url = s3_upload_file_pub(f, req->filename, req->content_type, s3_avatar_key, &s3_config);
    fclose(f);

    if (!url || *url == '\0')
    {
        *res = cHTTPX_ResJson(cHTTPX_StatusInternalServerError, "{\"error\": \"%s\"}", cHTTPX_i18n_t("error.save-file", ctx->lang));
        goto cleanup;
    }

    /* update in database */
    db_result_t user_upd_result = db_user_profile_upd_avatar_by_uid(http_server->conn, ctx->user->user_uid, url);

    switch (user_upd_result)
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
    if (url)
        free(url);

    if (req->filename[0] != '\0')
        remove(req->filename);

    return;
}

void user_get_profile_handler_v2(chttpx_request_t* req, chttpx_response_t* res)
{
    auth_token_t* ctx = (auth_token_t*)req->context;

    /* DB. get user info */
    user_info_t* user = NULL;

    /* Get from params -> user_uid */
    const char* user_uid_param = cHTTPX_Param(req, "user_uid");
    if (!user_uid_param || *user_uid_param == '\0')
    {
        *res = cHTTPX_ResJson(cHTTPX_StatusBadRequest, "{\"error\": \"%s\"}", cHTTPX_i18n_t("error.user-not-found", ctx->lang));
        goto cleanup;
    }

    uint64_t user_uid = strtoull(user_uid_param, NULL, 10);

    user = db_user_info_get_by_uid(http_server->conn, user_uid);
    if (!user)
    {
        *res = cHTTPX_ResJson(cHTTPX_StatusNotFound, "{\"error\": \"%s\"}", cHTTPX_i18n_t("error.user-not-found", ctx->lang));
        goto cleanup;
    }

    *res = cHTTPX_ResJson(cHTTPX_StatusOK,
                          "{"
                          "\"message\": {"
                          "\"id\": %lu,"
                          "\"created_at\": %ld,"
                          "\"user_uid\": %lu,"
                          "\"avatar\": \"%s\","
                          "\"name\": \"%s\","
                          "\"username\": \"%s\","
                          "\"username_visible\": %s,"
                          "\"email\": \"%s\","
                          "\"email_visible\": %s,"
                          "\"email_confirm\": %s,"
                          "\"phone\": \"%s\","
                          "\"phone_visible\": %s,"
                          "\"overview\": \"%s\""
                          "}"
                          "}",
                          user->id, user->created_at, user->user_uid, user->avatar ? user->avatar : "", user->name ? user->name : "",
                          user->username ? user->username : "", user->username_visible ? "true" : "false", user->email ? user->email : "",
                          user->email_visible ? "true" : "false", user->email_confirm ? "true" : "false", user->phone ? user->phone : "",
                          user->phone_visible ? "true" : "false", user->overview ? user->overview : "");

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
    auth_token_t* ctx = (auth_token_t*)req->context;

    user_profile_update_t payload = {0};

    chttpx_validation_t fields[] = {
        chttpx_validation_string("username", &payload.username, false, 4, 24, VALIDATOR_NONE),
        chttpx_validation_boolean("username_visible", &payload.username, false),
        chttpx_validation_string("name", &payload.name, false, 2, 24, VALIDATOR_NONE),
        chttpx_validation_string("email", &payload.email, false, 5, 254, VALIDATOR_EMAIL),
        chttpx_validation_boolean("email_visible", &payload.email_visible, false),
        chttpx_validation_string("phone", &payload.phone, false, 8, 254, VALIDATOR_NONE),
        chttpx_validation_boolean("phone_visible", &payload.phone_visible, false),
        chttpx_validation_string("overview", &payload.overview, false, 1, 125, VALIDATOR_NONE),
        chttpx_validation_string("password", &payload.password, false, 8, 16, VALIDATOR_NONE),
    };

    if (!cHTTPX_Parse(req, fields, (sizeof(fields) / sizeof(fields[0]))))
        goto errorjson;

    if (!cHTTPX_Validate(req, fields, (sizeof(fields) / sizeof(fields[0])), ctx->lang))
        goto errorjson;

    /* Trim spaces */
    if (payload.username)
        trim_space(payload.username);

    if (payload.email)
    {
        trim_space(payload.email);
        to_lower(payload.email);
    }

    /* Validate username */
    if (payload.username && !is_valid(payload.username))
    {
        *res = cHTTPX_ResJson(cHTTPX_StatusBadRequest, "{\"error\": \"%s\"}", cHTTPX_i18n_t("error.invalid-username", ctx->lang));
        goto cleanup;
    }

    char* password_hash = NULL;

    if (payload.password)
    {
        /* Trim space password */
        trim_space(payload.password);

        if (is_simple_password(payload.password))
        {
            *res = cHTTPX_ResJson(cHTTPX_StatusBadRequest, "{\"error\": \"%s\"}", cHTTPX_i18n_t("error.weak-password", ctx->lang));
            goto cleanup;
        }

        password_hash = hash_password(payload.password);
        if (!password_hash)
        {
            *res = cHTTPX_ResJson(cHTTPX_StatusInternalServerError, "{\"error\": \"%s\"}", cHTTPX_i18n_t("error.password-hash-failed", ctx->lang));
            goto cleanup;
        }

        payload.password = password_hash;
        password_hash = NULL;
    }

    db_result_t user_db_result = db_user_UPDATE_upd(http_server->conn, ctx->user->user_uid, &payload);

    switch (user_db_result)
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

    *res = cHTTPX_ResJson(cHTTPX_StatusOK, "{\"message\": \"%s\"}", cHTTPX_i18n_t("profile-updated", ctx->lang));

cleanup:
    /* Payloads free */
    free(payload.username);
    free(payload.name);
    free(payload.email);
    free(payload.phone);
    free(payload.overview);
    free(payload.password);

    return;

errorjson:
    *res = cHTTPX_ResJson(cHTTPX_StatusBadRequest, "{\"error\": \"%s\"}", req->error_msg);

    goto cleanup;
}