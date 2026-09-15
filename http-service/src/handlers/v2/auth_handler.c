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
    /* Context in request */
    auth_token_t* ctx = (auth_token_t*)req->context;

    auth_confirm_email_t payload = {0};

    chttpx_validation_t fields[] = {
        chttpx_validation_string("email", &payload.email, true, 5, 254, VALIDATOR_EMAIL),
    };

    if (!cHTTPX_Parse(req, fields, (sizeof(fields) / sizeof(fields[0]))))
        goto errorjson;

    if (!cHTTPX_Validate(req, fields, (sizeof(fields) / sizeof(fields[0])), ctx->lang))
        goto errorjson;

    /* Trim spaces */
    trim_space(payload.email);
    /* To lower email */
    to_lower(payload.email);

    /* Check limits verification code (24h.) */
    if (redis_check_limit_email_and_increment(payload.email, MAX_EMAIL_LIMITS_24H) != 1)
    {
        logger_error("auth_confirm_email_handler_v2 req={%s}: exceeded daily limit (:email)", ctx->x_req_id);

        *res = cHTTPX_ResJson(cHTTPX_StatusTooManyRequests, "{\"error\": \"%s\"}", cHTTPX_i18n_t("error.email-limit-24-hourse", ctx->lang));
        goto cleanup;
    }

    uint64_t code;
    if (RAND_bytes((unsigned char*)&code, sizeof(code)) != 1)
    {
        unsigned long err_code = ERR_get_error();
        char err_buf[256];
        ERR_error_string_n(err_code, err_buf, sizeof(err_buf));

        logger_error("auth_confirm_email_handler_v2 req={%s}: failed to generate CODE: %s", ctx->x_req_id, err_buf);
        *res = cHTTPX_ResJson(cHTTPX_StatusInternalServerError, "{\"error\": \"%s\"}", cHTTPX_i18n_t("error.code-generate-failed", ctx->lang));

        goto cleanup;
    }
    code = (code % 900000) + 100000;

    if (redis_verifycode_create(payload.email, code) != 0)
    {
        *res = cHTTPX_ResJson(cHTTPX_StatusInternalServerError, "{\"error\": \"%s\"}", cHTTPX_i18n_t("error.code-save-failed", ctx->lang));
        goto cleanup;
    }

    logger_info("auth_confirm_email_handler_v2 req={%s}: email={%s} code={%lu}", ctx->x_req_id, payload.email, code);

    /* Send email */
    /* ---------- */
    const char* html = "<body>"
                       "<p>%s <b>%s</b></p>"
                       "<p>%s</p>"
                       "<h2>%lu</h2>"
                       "<p>%s...</p>"
                       "<p>%s</p>"
                       "</body>";

    char buffer[2048];
    snprintf(buffer, sizeof(buffer), html, cHTTPX_i18n_t("email.request-notice", ctx->lang), payload.email,
             cHTTPX_i18n_t("email.code-instruction", ctx->lang), code, cHTTPX_i18n_t("email.code-expire", ctx->lang),
             cHTTPX_i18n_t("email.footer", ctx->lang));

    if (send_email_async(payload.email, cHTTPX_i18n_t("email.subject", ctx->lang), buffer, NULL) != 0)
    {
        *res = cHTTPX_ResJson(cHTTPX_StatusInternalServerError, "{\"error\": \"%s: %s\"}", cHTTPX_i18n_t("error.sending-email", ctx->lang),
                              payload.email);
        goto cleanup;
    }
    /* ---------- */
    /* Send email */

    *res = cHTTPX_ResJson(cHTTPX_StatusOK, "{\"message\": \"%s\"}", cHTTPX_i18n_t("code-sent", ctx->lang));

cleanup:
    /* Free payloads */
    free(payload.email);

    return;

errorjson:
    *res = cHTTPX_ResJson(cHTTPX_StatusBadRequest, "{\"error\": \"%s\"}", req->error_msg);

    goto cleanup;
}

void auth_logout_handler_v2(chttpx_request_t* req, chttpx_response_t* res)
{
    const char* authorization = cHTTPX_HeaderGet(req, "Authorization");
    if (!authorization || strncasecmp(authorization, "Bearer ", 7) != 0 || !authorization[7])
    {
        *res = cHTTPX_ResJson(cHTTPX_StatusUnauthorized, "{\"error\": \"session required\"}");
        return;
    }

    if (!redis_session_delete(authorization + 7))
    {
        *res = cHTTPX_ResJson(cHTTPX_StatusInternalServerError, "{\"error\": \"session revocation failed\"}");
        return;
    }

    *res = cHTTPX_ResJson(cHTTPX_StatusNoContent, NULL);
}

void auth_login_handler_v2(chttpx_request_t* req, chttpx_response_t* res)
{
    /* Context in request */
    auth_token_t* ctx = (auth_token_t*)req->context;

    /* Initial session id */
    char* session_id = NULL;

    auth_login_t payload = {0};

    chttpx_validation_t fields[] = {
        chttpx_validation_string("email", &payload.email, true, 5, 254, VALIDATOR_EMAIL),
        chttpx_validation_string("password", &payload.password, false, 8, 16, VALIDATOR_NONE),
    };

    /* DB. get user core */
    user_core_t* user = NULL;

    if (!cHTTPX_Parse(req, fields, (sizeof(fields) / sizeof(fields[0]))))
        goto errorjson;

    if (!cHTTPX_Validate(req, fields, (sizeof(fields) / sizeof(fields[0])), ctx->lang))
        goto errorjson;

    /* Trim spaces */
    trim_space(payload.email);
    /* To lower email */
    to_lower(payload.email);

    user = db_user_core_get_by_email(http_server->conn, payload.email);
    if (!user)
    {
        *res = cHTTPX_ResJson(cHTTPX_StatusNotFound, "{\"error\": \"%s\"}", cHTTPX_i18n_t("error.user-not-found", ctx->lang));
        goto cleanup;
    }

    /* If user exist password, but not in payload -> 202 */
    if (user->password && user->password[0] != '\0' && !payload.password)
    {
        *res = cHTTPX_ResJson(cHTTPX_StatusAccepted, "{\"message\": \"%s\"}", cHTTPX_i18n_t("error.password-required", ctx->lang));
        goto cleanup;
    }

    if (user->password && user->password[0] != '\0' && !verify_password(payload.password, user->password))
    {
        *res = cHTTPX_ResJson(cHTTPX_StatusUnauthorized, "{\"error\": \"%s\"}", cHTTPX_i18n_t("error.incorrect-login-data", ctx->lang));
        goto cleanup;
    }


    session_t session = {.user_uid = user->user_uid, .expires_at = time(NULL) + REDIS_SESSION_TTL};

    session_id = redis_session_create(&session);
    if (!session_id)
    {
        *res = cHTTPX_ResJson(cHTTPX_StatusInternalServerError, "{\"error\": \"%s\"}", cHTTPX_i18n_t("error.session-creation-error", ctx->lang));
        goto cleanup;
    }

    char* safe_session = escape_json_string(session_id);
    if (safe_session)
    {
        *res = cHTTPX_ResJson(cHTTPX_StatusOK, "{\"message\": \"%s\"}", safe_session);
        free(safe_session);
    }
    else
    {
        *res = cHTTPX_ResJson(cHTTPX_StatusInternalServerError, "{\"error\": \"%s\"}", cHTTPX_i18n_t("error.session-creation-error", ctx->lang));
    }

cleanup:
    /* Free payloads */
    free(payload.email);
    if (payload.password)
        free(payload.password);

    if (user)
    {
        free(user->email);
        free(user->password);
        free(user);
        user = NULL;
    }

    if (session_id)
        free(session_id);

    return;

errorjson:
    *res = cHTTPX_ResJson(cHTTPX_StatusBadRequest, "{\"error\": \"%s\"}", req->error_msg);

    goto cleanup;
}

void auth_create_handler_v2(chttpx_request_t* req, chttpx_response_t* res)
{
    auth_token_t* ctx = (auth_token_t*)req->context;

    /* Initial session id */
    char* session_id = NULL;

    auth_create_t payload = {0};

    chttpx_validation_t fields[] = {
        chttpx_validation_string("name", &payload.name, true, 2, 24, VALIDATOR_NONE),
        chttpx_validation_string("username", &payload.username, true, 4, 24, VALIDATOR_NONE),
        chttpx_validation_string("email", &payload.email, true, 5, 254, VALIDATOR_EMAIL),
        chttpx_validation_string("password", &payload.password, false, 8, 16, VALIDATOR_NONE),
    };

    /* DB. get user core */
    user_core_t* user = NULL;
    char* password_hash = NULL;
    user_core_t* user_core = NULL;
    user_profile_t* user_profile = NULL;
    user_profile_access_t* user_profile_access = NULL;
    user_active_t* user_active = NULL;

    if (!cHTTPX_Parse(req, fields, (sizeof(fields) / sizeof(fields[0]))))
        goto errorjson;

    if (!cHTTPX_Validate(req, fields, (sizeof(fields) / sizeof(fields[0])), ctx->lang))
        goto errorjson;

    /* Trim spaces */
    trim_space(payload.username);
    trim_space(payload.email);

    /* Check symbols */
    if (!is_valid(payload.username))
    {
        *res = cHTTPX_ResJson(cHTTPX_StatusBadRequest, "{\"error\": \"%s\"}", cHTTPX_i18n_t("error.invalid-username", ctx->lang));
        goto cleanup;
    }

    /* Trim spaces */
    trim_space(payload.email);
    /* To lower email */
    to_lower(payload.email);

    /* Check user is exists */
    user = db_user_core_get_by_email(http_server->conn, payload.email);
    if (user)
    {
        *res = cHTTPX_ResJson(cHTTPX_StatusBadRequest, "{\"error\": \"%s\"}", cHTTPX_i18n_t("error.user-already-registered", ctx->lang));

        free(user->email);
        free(user->password);
        free(user);
        user = NULL;

        goto cleanup;
    }

    /* Validation password and hash password */
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
    }

    uint64_t uid;
    if (RAND_bytes((unsigned char*)&uid, sizeof(uid)) != 1)
    {
        unsigned long err_code = ERR_get_error();
        char err_buf[256];
        ERR_error_string_n(err_code, err_buf, sizeof(err_buf));

        logger_error("auth_create_handler_v2 req={%s}: failed to generate UID: %s", ctx->x_req_id, err_buf);
        *res = cHTTPX_ResJson(cHTTPX_StatusInternalServerError, "{\"error\": \"%s\"}", cHTTPX_i18n_t("error.user-uid-generate-failed", ctx->lang));

        goto cleanup;
    }
    uid = (uid % 9000000000ULL) + 1000000000ULL;

    /* calloc user_core */
    user_core = calloc(1, sizeof(user_core_t));
    if (!user_core)
    {
        logger_error("auth_create_handler_v2 req={%s}: calloc failed for user_core_t", ctx->x_req_id);

        fprintf(stderr, "calloc failed\n");
        *res = cHTTPX_ResJson(cHTTPX_StatusInternalServerError, "{\"error\": \"%s\"}", cHTTPX_i18n_t("error.something-went-wrong", ctx->lang));

        goto cleanup;
    }
    user_core->user_uid = uid;
    user_core->email = strdup(payload.email);
    user_core->email_confirm = true;
    user_core->password = password_hash ? strdup(password_hash) : NULL;

    /* calloc user_profile */
    user_profile = calloc(1, sizeof(user_profile_t));
    if (!user_profile)
    {
        logger_error("auth_create_handler_v2 req={%s}: calloc failed for user_profile_t", ctx->x_req_id);

        fprintf(stderr, "calloc failed\n");
        *res = cHTTPX_ResJson(cHTTPX_StatusInternalServerError, "{\"error\": \"%s\"}", cHTTPX_i18n_t("error.something-went-wrong", ctx->lang));

        goto cleanup;
    }
    user_profile->user_uid = uid;
    user_profile->name = strdup(payload.name);
    user_profile->username = strdup(payload.username);
    user_profile->avatar = NULL;

    /* calloc user_profile_access */
    user_profile_access = calloc(1, sizeof(user_profile_access_t));
    if (!user_profile_access)
    {
        logger_error("auth_create_handler_v2 req={%s}: calloc failed for user_profile_access_t", ctx->x_req_id);

        fprintf(stderr, "calloc failed\n");
        *res = cHTTPX_ResJson(cHTTPX_StatusInternalServerError, "{\"error\": \"%s\"}", cHTTPX_i18n_t("error.something-went-wrong", ctx->lang));

        goto cleanup;
    }
    user_profile_access->user_uid = uid;
    user_profile_access->username_visible = true;
    user_profile_access->email_visible = true;
    user_profile_access->phone_visible = false;

    /* calloc user_active */
    user_active = calloc(1, sizeof(user_active_t));
    if (!user_active)
    {
        logger_error("auth_create_handler_v2 req={%s}: calloc failed for user_active_t", ctx->x_req_id);

        fprintf(stderr, "calloc failed\n");
        *res = cHTTPX_ResJson(cHTTPX_StatusInternalServerError, "{\"error\": \"%s\"}", cHTTPX_i18n_t("error.something-went-wrong", ctx->lang));

        goto cleanup;
    }
    user_active->user_uid = uid;


    db_result_t user_db_result = db_user_create(http_server->conn, user_core, user_profile, user_profile_access, user_active);

    /* free memory */
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

    session_t session = {.user_uid = uid, .expires_at = time(NULL) + REDIS_SESSION_TTL};

    session_id = redis_session_create(&session);
    if (!session_id)
    {
        *res = cHTTPX_ResJson(cHTTPX_StatusInternalServerError, "{\"error\": \"%s\"}", cHTTPX_i18n_t("error.session-creation-error", ctx->lang));
        goto cleanup;
    }

    char* safe_session = escape_json_string(session_id);
    if (safe_session)
    {
        *res = cHTTPX_ResJson(cHTTPX_StatusCreated, "{\"message\": \"%s\"}", safe_session);
        free(safe_session);
    }
    else
    {
        *res = cHTTPX_ResJson(cHTTPX_StatusInternalServerError, "{\"error\": \"%s\"}", cHTTPX_i18n_t("error.session-creation-error", ctx->lang));
    }

cleanup:
    /* Free payloads */
    free(payload.name);
    free(payload.username);
    free(payload.email);

    if (payload.password)
        free(payload.password);

    if (password_hash)
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

    if (user_profile_access)
        free(user_profile_access);

    if (user_active)
        free(user_active);

    if (session_id)
        free(session_id);

    return;

errorjson:
    *res = cHTTPX_ResJson(cHTTPX_StatusBadRequest, "{\"error\": \"%s\"}", req->error_msg);

    goto cleanup;
}

void auth_verify_handler_v2(chttpx_request_t* req, chttpx_response_t* res)
{
    auth_token_t* ctx = (auth_token_t*)req->context;

    /* Initial session id */
    char* session_id = NULL;

    auth_verify_t payload = {0};

    chttpx_validation_t fields[] = {
        chttpx_validation_string("email", &payload.email, true, 5, 254, VALIDATOR_EMAIL),
        chttpx_validation_integer("code", &payload.code, true),
    };

    /* DB. get user core */
    user_core_t* user = NULL;

    if (!cHTTPX_Parse(req, fields, (sizeof(fields) / sizeof(fields[0]))))
        goto errorjson;

    if (!cHTTPX_Validate(req, fields, (sizeof(fields) / sizeof(fields[0])), ctx->lang))
        goto errorjson;

    trim_space(payload.email);
    to_lower(payload.email);

    int code;
    if (!redis_verifycode_get(payload.email, &code) || payload.code != code)
    {
        *res = cHTTPX_ResJson(cHTTPX_StatusBadRequest, "{\"error\": \"%s\"}", cHTTPX_i18n_t("error.invalid-confirmation-code", ctx->lang));
        goto cleanup;
    }

    user = db_user_core_get_by_email(http_server->conn, payload.email);
    if (!user || (user->password && user->password[0] != '\0'))
    {
        redis_mark_email_confirmed(payload.email);
        *res = cHTTPX_ResJson(cHTTPX_StatusAccepted, "{\"message\": \"%s\"}", user ? "password-required" : "registration-required");
        goto cleanup;
    }

    session_t session = {.user_uid = user->user_uid, .expires_at = time(NULL) + REDIS_SESSION_TTL};

    session_id = redis_session_create(&session);
    if (!session_id)
    {
        *res = cHTTPX_ResJson(cHTTPX_StatusInternalServerError, "{\"error\": \"%s\"}", cHTTPX_i18n_t("error.session-creation-error", ctx->lang));
        goto cleanup;
    }

    char* safe_session = escape_json_string(session_id);
    if (safe_session)
    {
        *res = cHTTPX_ResJson(cHTTPX_StatusOK, "{\"message\": \"%s\"}", safe_session);
        free(safe_session);
    }
    else
    {
        *res = cHTTPX_ResJson(cHTTPX_StatusInternalServerError, "{\"error\": \"%s\"}", cHTTPX_i18n_t("error.session-creation-error", ctx->lang));
    }

cleanup:
    /* Free payloads */
    free(payload.email);

    if (user)
    {
        free(user->email);
        free(user->password);
        free(user);
        user = NULL;
    }

    if (session_id)
        free(session_id);

    return;

errorjson:
    *res = cHTTPX_ResJson(cHTTPX_StatusBadRequest, "{\"error\": \"%s\"}", req->error_msg);

    goto cleanup;
}

void auth_delete_handler_v2(chttpx_request_t* req, chttpx_response_t* res)
{
    auth_token_t* ctx = (auth_token_t*)req->context;

    db_result_t user_del_db_result = db_user_del_by_uid(http_server->conn, ctx->user->user_uid);

    switch (user_del_db_result)
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

    /* DELETE AVATAR S3 */
    s3_config_t s3_config = {
        .endpoint = getenv("S3_ENDPOINT"),
        .bucket = getenv("S3_BUCKET_PUB"),
        .access_key = getenv("S3_ACCESS_KEY"),
        .secret_key = getenv("S3_SECRET_KEY"),
        .region = getenv("S3_REGION"),
    };

    /* save to s3 storage */
    if (s3_delete_file(ctx->user->avatar, &s3_config) != 0)
    {
        *res = cHTTPX_ResJson(cHTTPX_StatusConflict, "{\"error\": \"%s\"}", cHTTPX_i18n_t("error.s3-cloud", ctx->lang));
        goto cleanup;
    }

    *res = cHTTPX_ResJson(cHTTPX_StatusOK, "{\"message\": \"%s\"}", cHTTPX_i18n_t("user-deleted", ctx->lang));

cleanup:

    return;
}
