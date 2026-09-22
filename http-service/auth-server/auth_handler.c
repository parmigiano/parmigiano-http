#include "handlers.h"

#include "s3.h"
#include "httpx.h"
#include "logger.h"
#include "utilities.h"
#include "redis/redis.h"
#include "redis/redis_limits.h"
#include "redis/redis_session.h"
#include "redis/redis_verifycode.h"
#include "redis/redis_email_confirm.h"
#include "postgres/postgres_users.h"

#include <time.h>
#include <stdbool.h>
#include <inttypes.h>
#include <openssl/err.h>
#include <openssl/rand.h>

#define MAX_EMAIL_LIMITS_24H 5

typedef struct
{
    char* email;
} auth_confirm_email_t;

typedef struct
{
    char* email;
    char* password;
} auth_login_t;

typedef struct
{
    char* name;
    char* username;
    char* email;
    char* password;
} auth_create_t;

typedef struct
{
    char* email;
    int code;
} auth_verify_t;

void auth_confirm_email_handler_v2(chttpx_request_t* req, chttpx_response_t* res)
{
    auth_confirm_email_t payload = {0};

    chttpx_validation_t fields[] = {
        cHTTPX_StringField("email", &payload.email, true, 5, 254, CHTTPX_TRIM | CHTTPX_LOWERCASE, validate_email),
    };

    if (!bind_json_i18n(req, res, fields, CHTTPX_ARRAY_LEN(fields)))
        return;

    if (redis_check_limit_email_and_increment(payload.email, MAX_EMAIL_LIMITS_24H) != 1)
    {
        logger_error("auth_confirm_email_handler_v2 req={%s}: exceeded daily limit (:email)", req->request_id);
        *res = cHTTPX_ResError(cHTTPX_StatusTooManyRequests, cHTTPX_i18n_t("error.email-limit-24-hourse", req->language));
        return;
    }

    uint64_t code;
    if (RAND_bytes((unsigned char*)&code, sizeof(code)) != 1)
    {
        unsigned long err_code = ERR_get_error();
        char err_buf[256];

        ERR_error_string_n(err_code, err_buf, sizeof(err_buf));

        logger_error("auth_confirm_email_handler_v2 req={%s}: failed to generate CODE: %s", req->request_id, err_buf);
        *res = cHTTPX_ResError(cHTTPX_StatusInternalServerError, cHTTPX_i18n_t("error.code-generate-failed", req->language));
        return;
    }
    code = (code % 900000) + 100000;

    if (redis_verifycode_create(payload.email, code) != 0)
    {
        *res = cHTTPX_ResError(cHTTPX_StatusInternalServerError, cHTTPX_i18n_t("error.code-save-failed", req->language));
        return;
    }

    logger_info("auth_confirm_email_handler_v2 req={%s}: email={%s} code={%" PRIu64 "}", req->request_id, payload.email, code);

    const char* html = "<body>"
                       "<p>%s <b>%s</b></p>"
                       "<p>%s</p>"
                       "<h2>%" PRIu64 "</h2>"
                       "<p>%s...</p>"
                       "<p>%s</p>"
                       "</body>";

    char buffer[2048];
    snprintf(buffer, sizeof(buffer), html, cHTTPX_i18n_t("email.request-notice", req->language), payload.email,
             cHTTPX_i18n_t("email.code-instruction", req->language), code, cHTTPX_i18n_t("email.code-expire", req->language),
             cHTTPX_i18n_t("email.footer", req->language));

    if (send_email_async(payload.email, cHTTPX_i18n_t("email.subject", req->language), buffer, NULL) != 0)
    {
        *res = cHTTPX_ResError(cHTTPX_StatusInternalServerError, cHTTPX_i18n_t("error.sending-email", req->language));
        return;
    }

    *res = cHTTPX_ResMessage(cHTTPX_StatusOK, cHTTPX_i18n_t("code-sent", req->language));
}

void auth_logout_handler_v2(chttpx_request_t* req, chttpx_response_t* res)
{
    const char* session_id = cHTTPX_BearerToken(req);
    if (!session_id)
    {
        *res = cHTTPX_ResError(cHTTPX_StatusUnauthorized, cHTTPX_i18n_t("error.connect-to-account", req->language));
        return;
    }

    if (!redis_session_delete(session_id))
    {
        *res = cHTTPX_ResError(cHTTPX_StatusInternalServerError, cHTTPX_i18n_t("error.something-went-wrong", req->language));
        return;
    }

    *res = cHTTPX_ResNoContent();
}

void auth_login_handler_v2(chttpx_request_t* req, chttpx_response_t* res)
{
    char* session_id = NULL;
    auth_login_t payload = {0};

    chttpx_validation_t fields[] = {
        cHTTPX_StringField("email", &payload.email, true, 5, 254, CHTTPX_TRIM | CHTTPX_LOWERCASE, validate_email),
        cHTTPX_StringField("password", &payload.password, false, 8, 16, CHTTPX_NORMALIZE_NONE, NULL),
    };

    user_core_t* user = NULL;

    if (!bind_json_i18n(req, res, fields, CHTTPX_ARRAY_LEN(fields)))
        return;

    user = db_user_core_get_by_email(app_context->conn, payload.email);
    if (!user)
    {
        *res = cHTTPX_ResError(cHTTPX_StatusNotFound, cHTTPX_i18n_t("error.user-not-found", req->language));
        goto cleanup;
    }

    if (user->password && user->password[0] != '\0' && !payload.password)
    {
        *res = cHTTPX_ResMessage(cHTTPX_StatusAccepted, cHTTPX_i18n_t("error.password-required", req->language));
        goto cleanup;
    }

    if (user->password && user->password[0] != '\0' && !verify_password(payload.password, user->password))
    {
        *res = cHTTPX_ResError(cHTTPX_StatusUnauthorized, cHTTPX_i18n_t("error.incorrect-login-data", req->language));
        goto cleanup;
    }

    session_t session = {.user_uid = user->user_uid, .expires_at = time(NULL) + REDIS_SESSION_TTL};

    session_id = redis_session_create(&session);
    if (!session_id)
    {
        *res = cHTTPX_ResError(cHTTPX_StatusInternalServerError, cHTTPX_i18n_t("error.session-creation-error", req->language));
        goto cleanup;
    }

    *res = cHTTPX_ResMessage(cHTTPX_StatusOK, session_id);

cleanup:
    if (user)
    {
        free(user->email);
        free(user->password);
        free(user);
    }

    free(session_id);
}

void auth_create_handler_v2(chttpx_request_t* req, chttpx_response_t* res)
{
    char* session_id = NULL;
    auth_create_t payload = {0};

    chttpx_validation_t fields[] = {
        cHTTPX_StringField("name", &payload.name, true, 0, 96, CHTTPX_TRIM, validate_name),
        cHTTPX_StringField("username", &payload.username, true, 4, 24, CHTTPX_TRIM, validate_username),
        cHTTPX_StringField("email", &payload.email, true, 5, 254, CHTTPX_TRIM | CHTTPX_LOWERCASE, validate_email),
        cHTTPX_StringField("password", &payload.password, false, 8, 16, CHTTPX_NORMALIZE_NONE, validate_password),
    };

    user_core_t* user = NULL;
    char* password_hash = NULL;
    user_core_t* user_core = NULL;
    user_profile_t* user_profile = NULL;
    user_profile_access_t* user_profile_access = NULL;
    user_active_t* user_active = NULL;

    if (!bind_json_i18n(req, res, fields, CHTTPX_ARRAY_LEN(fields)))
        return;

    user = db_user_core_get_by_email(app_context->conn, payload.email);
    if (user)
    {
        *res = cHTTPX_ResError(cHTTPX_StatusBadRequest, cHTTPX_i18n_t("error.user-already-registered", req->language));

        free(user->email);
        free(user->password);
        free(user);
        user = NULL;
        goto cleanup;
    }

    if (payload.password)
    {
        password_hash = hash_password(payload.password);
        if (!password_hash)
        {
            *res = cHTTPX_ResError(cHTTPX_StatusInternalServerError, cHTTPX_i18n_t("error.password-hash-failed", req->language));
            goto cleanup;
        }
    }

    uint64_t uid;
    if (RAND_bytes((unsigned char*)&uid, sizeof(uid)) != 1)
    {
        unsigned long err_code = ERR_get_error();
        char err_buf[256];
        ERR_error_string_n(err_code, err_buf, sizeof(err_buf));

        logger_error("auth_create_handler_v2 req={%s}: failed to generate UID: %s", req->request_id, err_buf);
        *res = cHTTPX_ResError(cHTTPX_StatusInternalServerError, cHTTPX_i18n_t("error.user-uid-generate-failed", req->language));
        goto cleanup;
    }

    uid = (uid % 9000000000ULL) + 1000000000ULL;

    user_core = calloc(1, sizeof(user_core_t));
    if (!user_core)
    {
        logger_error("auth_create_handler_v2 req={%s}: calloc failed for user_core_t", req->request_id);
        *res = cHTTPX_ResError(cHTTPX_StatusInternalServerError, cHTTPX_i18n_t("error.something-went-wrong", req->language));
        goto cleanup;
    }
    user_core->user_uid = uid;
    user_core->email = strdup(payload.email);
    user_core->email_confirm = true;
    user_core->password = password_hash ? strdup(password_hash) : NULL;

    user_profile = calloc(1, sizeof(user_profile_t));
    if (!user_profile)
    {
        logger_error("auth_create_handler_v2 req={%s}: calloc failed for user_profile_t", req->request_id);
        *res = cHTTPX_ResError(cHTTPX_StatusInternalServerError, cHTTPX_i18n_t("error.something-went-wrong", req->language));
        goto cleanup;
    }
    user_profile->user_uid = uid;
    user_profile->name = strdup(payload.name);
    user_profile->username = strdup(payload.username);
    user_profile->avatar = NULL;

    user_profile_access = calloc(1, sizeof(user_profile_access_t));
    if (!user_profile_access)
    {
        logger_error("auth_create_handler_v2 req={%s}: calloc failed for user_profile_access_t", req->request_id);
        *res = cHTTPX_ResError(cHTTPX_StatusInternalServerError, cHTTPX_i18n_t("error.something-went-wrong", req->language));
        goto cleanup;
    }
    user_profile_access->user_uid = uid;
    user_profile_access->username_visible = true;
    user_profile_access->email_visible = true;
    user_profile_access->phone_visible = false;

    user_active = calloc(1, sizeof(user_active_t));
    if (!user_active)
    {
        logger_error("auth_create_handler_v2 req={%s}: calloc failed for user_active_t", req->request_id);
        *res = cHTTPX_ResError(cHTTPX_StatusInternalServerError, cHTTPX_i18n_t("error.something-went-wrong", req->language));
        goto cleanup;
    }
    user_active->user_uid = uid;

    db_result_t user_db_result = db_user_create(app_context->conn, user_core, user_profile, user_profile_access, user_active);

    free(user_core->email);
    free(user_core->password);
    free(user_core);
    user_core = NULL;

    free(user_profile->name);
    free(user_profile->username);
    free(user_profile);
    user_profile = NULL;

    free(user_profile_access);
    user_profile_access = NULL;

    free(user_active);
    user_active = NULL;

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

    session_t session = {.user_uid = uid, .expires_at = time(NULL) + REDIS_SESSION_TTL};

    session_id = redis_session_create(&session);
    if (!session_id)
    {
        *res = cHTTPX_ResError(cHTTPX_StatusInternalServerError, cHTTPX_i18n_t("error.session-creation-error", req->language));
        goto cleanup;
    }

    *res = cHTTPX_ResMessage(cHTTPX_StatusCreated, session_id);

cleanup:
    free(password_hash);

    if (user_core)
    {
        free(user_core->email);
        free(user_core->password);
        free(user_core);
    }

    if (user_profile)
    {
        free(user_profile->name);
        free(user_profile->username);
        free(user_profile);
    }

    free(user_profile_access);
    free(user_active);
    free(session_id);
}

void auth_verify_handler_v2(chttpx_request_t* req, chttpx_response_t* res)
{
    char* session_id = NULL;
    auth_verify_t payload = {0};

    chttpx_validation_t fields[] = {
        cHTTPX_StringField("email", &payload.email, true, 5, 254, CHTTPX_TRIM | CHTTPX_LOWERCASE, validate_email),
        chttpx_validation_integer("code", &payload.code, true),
    };

    user_core_t* user = NULL;

    if (!bind_json_i18n(req, res, fields, CHTTPX_ARRAY_LEN(fields)))
        return;

    int code;
    if (!redis_verifycode_get(payload.email, &code) || payload.code != code)
    {
        *res = cHTTPX_ResError(cHTTPX_StatusBadRequest, cHTTPX_i18n_t("error.invalid-confirmation-code", req->language));
        goto cleanup;
    }

    user = db_user_core_get_by_email(app_context->conn, payload.email);
    if (!user || (user->password && user->password[0] != '\0'))
    {
        redis_mark_email_confirmed(payload.email);
        *res = cHTTPX_ResMessage(cHTTPX_StatusAccepted, user ? "password-required" : "registration-required");
        goto cleanup;
    }

    session_t session = {.user_uid = user->user_uid, .expires_at = time(NULL) + REDIS_SESSION_TTL};

    session_id = redis_session_create(&session);
    if (!session_id)
    {
        *res = cHTTPX_ResError(cHTTPX_StatusInternalServerError, cHTTPX_i18n_t("error.session-creation-error", req->language));
        goto cleanup;
    }

    *res = cHTTPX_ResMessage(cHTTPX_StatusOK, session_id);

cleanup:
    if (user)
    {
        free(user->email);
        free(user->password);
        free(user);
    }

    free(session_id);
}

void auth_delete_handler_v2(chttpx_request_t* req, chttpx_response_t* res)
{
    auth_token_t* ctx = cHTTPX_ContextGet(req, AUTH_CONTEXT_NAME);
    if (!ctx || !ctx->user)
    {
        *res = cHTTPX_ResError(cHTTPX_StatusUnauthorized, cHTTPX_i18n_t("error.connect-to-account", req->language));
        return;
    }

    db_result_t user_del_db_result = db_user_del_by_uid(app_context->conn, ctx->user->user_uid);

    switch (user_del_db_result)
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

    s3_config_t s3_config = {
        .endpoint = getenv("S3_ENDPOINT"),
        .bucket = getenv("S3_BUCKET_PUB"),
        .access_key = getenv("S3_ACCESS_KEY"),
        .secret_key = getenv("S3_SECRET_KEY"),
        .region = getenv("S3_REGION"),
    };

    if (s3_delete_file(ctx->user->avatar, &s3_config) != 0)
    {
        *res = cHTTPX_ResError(cHTTPX_StatusConflict, cHTTPX_i18n_t("error.s3-cloud", req->language));
        return;
    }

    *res = cHTTPX_ResMessage(cHTTPX_StatusOK, cHTTPX_i18n_t("user-deleted", req->language));
}
