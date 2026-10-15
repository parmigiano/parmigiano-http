#include "handlers.h"

#include "s3.h"
#include "httpx.h"
#include "logger.h"
#include "profanity.h"
#include "utilities.h"
#include "redis/redis.h"
#include "redis/redis_auth_token.h"
#include "redis/redis_limits.h"
#include "redis/redis_session.h"
#include "redis/redis_user_cache.h"
#include "redis/redis_verifycode.h"
#include "postgres/postgres_users.h"
#include "postgres/validate.h"

#include <stdio.h>
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
    char* login_token;
} auth_login_t;

typedef struct
{
    char* name;
    char* username;
    char* email;
    char* password;
    char* registration_token;
} auth_create_t;

typedef struct
{
    char* email;
    int code;
} auth_verify_t;

static int user_has_password(const user_core_t* user)
{
    return user && user->password && user->password[0] != '\0';
}

static void auth_reply_token_status(chttpx_request_t* req, chttpx_response_t* res, redis_auth_token_status_t status)
{
    const char* key = "error.something-went-wrong";
    int http_status = cHTTPX_StatusInternalServerError;

    if (status == REDIS_AUTH_TOKEN_MISSING)
    {
        key = "error.token-missing";
        http_status = cHTTPX_StatusForbidden;
    }
    else if (status == REDIS_AUTH_TOKEN_EXPIRED)
    {
        key = "error.token-expired";
        http_status = cHTTPX_StatusForbidden;
    }
    else if (status == REDIS_AUTH_TOKEN_MISMATCH)
    {
        key = "error.token-mismatch";
        http_status = cHTTPX_StatusForbidden;
    }

    *res = cHTTPX_ResError(http_status, cHTTPX_i18n_t(key, req->language));
}

static int auth_reply_next(chttpx_request_t* req, chttpx_response_t* res, const char* next, const char* field, const char* value, int expires_in)
{
    chttpx_json_t* root = cHTTPX_JsonObject(req);
    if (!root || cHTTPX_JsonString(root, "next", next) != 0 || cHTTPX_JsonString(root, field, value) != 0 ||
        cHTTPX_JsonNumber(root, "expires_in", expires_in) != 0)
    {
        *res = cHTTPX_ResError(cHTTPX_StatusInternalServerError, cHTTPX_i18n_t("error.something-went-wrong", req->language));
        return 0;
    }

    *res = cHTTPX_ResJsonObject(cHTTPX_StatusOK, root);
    return 1;
}

void auth_confirm_email_handler_v2(chttpx_request_t* req, chttpx_response_t* res)
{
    auth_confirm_email_t payload = {0};

    chttpx_validation_t fields[] = {
        cHTTPX_StringField("email", &payload.email, true, 5, 254, CHTTPX_TRIM | CHTTPX_LOWERCASE, validate_email),
    };

    if (!bind_json_i18n(req, res, fields, CHTTPX_ARRAY_LEN(fields)))
        return;

    /* Check limits verification code (24h.) */
    if (redis_check_limit_email(payload.email, MAX_EMAIL_LIMITS_24H) != 1)
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

    logger_info("auth_confirm_email_handler_v2 req={%s}: email={%s}", req->request_id, payload.email);

    /* Send email */
    /* ---------- */
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

    if (send_email(payload.email, cHTTPX_i18n_t("email.subject", req->language), buffer, NULL) != 0)
    {
        if (redis_verifycode_delete(payload.email) != 0)
            logger_error("auth_confirm_email_handler_v2 req={%s}: failed to delete confirmation code after send failure", req->request_id);

        *res = cHTTPX_ResError(cHTTPX_StatusInternalServerError, cHTTPX_i18n_t("error.sending-email", req->language));
        return;
    }
    /* ---------- */
    /* Send email */

    if (redis_increment_limit_email(payload.email) != 1)
        logger_error("auth_confirm_email_handler_v2 req={%s}: failed to count sent confirmation email", req->request_id);

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
    /* Initial session id */
    char* session_id = NULL;

    auth_login_t payload = {0};

    chttpx_validation_t fields[] = {
        cHTTPX_StringField("email", &payload.email, true, 5, 254, CHTTPX_TRIM | CHTTPX_LOWERCASE, validate_email),
        cHTTPX_StringField("password", &payload.password, false, 8, 128, CHTTPX_NORMALIZE_NONE, NULL),
        cHTTPX_StringField("login_token", &payload.login_token, false, 0, 64, CHTTPX_NORMALIZE_NONE, NULL),
    };

    /* DB. get user core */
    user_core_t* user = NULL;

    if (!bind_json_i18n(req, res, fields, CHTTPX_ARRAY_LEN(fields)))
        return;

    db_result_t lookup = db_user_core_lookup_by_email(app_context->db, payload.email, &user);
    if (db_reply_result(req, res, lookup))
        goto cleanup;

    if (!user)
    {
        *res = cHTTPX_ResError(cHTTPX_StatusUnauthorized, cHTTPX_i18n_t("error.incorrect-login-data", req->language));
        goto cleanup;
    }

    if (!user_has_password(user))
    {
        *res = cHTTPX_ResError(cHTTPX_StatusForbidden, cHTTPX_i18n_t("error.passwordless-login-denied", req->language));
        goto cleanup;
    }

    redis_auth_token_status_t token_status = redis_auth_token_check(REDIS_AUTH_TOKEN_LOGIN, payload.login_token, payload.email);
    if (token_status != REDIS_AUTH_TOKEN_OK)
    {
        auth_reply_token_status(req, res, token_status);
        goto cleanup;
    }

    if (!payload.password || !verify_password(payload.password, user->password))
    {
        *res = cHTTPX_ResError(cHTTPX_StatusUnauthorized, cHTTPX_i18n_t("error.incorrect-login-data", req->language));
        goto cleanup;
    }

    token_status = redis_auth_token_consume(REDIS_AUTH_TOKEN_LOGIN, payload.login_token, payload.email);
    if (token_status != REDIS_AUTH_TOKEN_OK)
    {
        auth_reply_token_status(req, res, token_status);
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

    if (session_id)
        free(session_id);
}

void auth_create_handler_v2(chttpx_request_t* req, chttpx_response_t* res)
{
    /* Initial session id */
    char* session_id = NULL;

    auth_create_t payload = {0};

    chttpx_validation_t fields[] = {
        cHTTPX_StringField("name", &payload.name, true, 0, 96, CHTTPX_TRIM, validate_name),
        cHTTPX_StringField("username", &payload.username, true, 4, 24, CHTTPX_TRIM, validate_username),
        cHTTPX_StringField("email", &payload.email, true, 5, 254, CHTTPX_TRIM | CHTTPX_LOWERCASE, validate_email),
        cHTTPX_StringField("password", &payload.password, false, 8, 128, CHTTPX_NORMALIZE_NONE, validate_password),
        cHTTPX_StringField("registration_token", &payload.registration_token, false, 0, 64, CHTTPX_NORMALIZE_NONE, NULL),
    };

    /* DB. get user core */
    user_core_t* user = NULL;
    char* password_hash = NULL;

    redis_auth_token_status_t token_status = REDIS_AUTH_TOKEN_ERROR;

    user_core_t* user_core = NULL;
    user_profile_t* user_profile = NULL;
    user_profile_access_t* user_profile_access = NULL;
    user_active_t* user_active = NULL;

    if (!bind_json_i18n(req, res, fields, CHTTPX_ARRAY_LEN(fields)))
        return;

    if (!payload.registration_token || payload.registration_token[0] == '\0')
    {
        auth_reply_token_status(req, res, REDIS_AUTH_TOKEN_MISSING);
        goto cleanup;
    }

    token_status = redis_auth_token_check(REDIS_AUTH_TOKEN_REGISTRATION, payload.registration_token, payload.email);
    if (token_status != REDIS_AUTH_TOKEN_OK)
    {
        auth_reply_token_status(req, res, token_status);
        goto cleanup;
    }

    const char* profanity_texts[] = {payload.name, payload.username};
    if (!profanity_reject_from_env(req, res, "auth.create", profanity_texts, 2))
        return;

    /* Check user is exists */
    user = db_user_core_get_by_email(app_context->db, payload.email);
    if (user)
    {
        *res = cHTTPX_ResError(cHTTPX_StatusBadRequest, cHTTPX_i18n_t("error.user-already-registered", req->language));

        free(user->email);
        free(user->password);
        free(user);
        user = NULL;

        goto cleanup;
    }

    /* Validation password and hash password */
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

    /* calloc user_core */
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

    /* calloc user_profile */
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

    /* calloc user_profile_access */
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

    /* calloc user_active */
    user_active = calloc(1, sizeof(user_active_t));
    if (!user_active)
    {
        logger_error("auth_create_handler_v2 req={%s}: calloc failed for user_active_t", req->request_id);

        *res = cHTTPX_ResError(cHTTPX_StatusInternalServerError, cHTTPX_i18n_t("error.something-went-wrong", req->language));

        goto cleanup;
    }
    user_active->user_uid = uid;

    db_result_t user_db_result = db_user_create(app_context->db, user_core, user_profile, user_profile_access, user_active);

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

    if (db_reply_result(req, res, user_db_result))
        goto cleanup;

    session_t session = {.user_uid = uid, .expires_at = time(NULL) + REDIS_SESSION_TTL};

    session_id = redis_session_create(&session);
    if (!session_id)
    {
        *res = cHTTPX_ResError(cHTTPX_StatusInternalServerError, cHTTPX_i18n_t("error.session-creation-error", req->language));
        goto cleanup;
    }

    *res = cHTTPX_ResMessage(cHTTPX_StatusCreated, session_id);

    token_status = redis_auth_token_consume(REDIS_AUTH_TOKEN_REGISTRATION, payload.registration_token, payload.email);
    if (token_status != REDIS_AUTH_TOKEN_OK)
        logger_error("auth_create_handler_v2 req={%s}: registration token consume after create failed", req->request_id);

cleanup:
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
}

void auth_verify_handler_v2(chttpx_request_t* req, chttpx_response_t* res)
{
    char* session_id = NULL;
    char flow_token[64];

    int code_held = 0;
    int code_ttl = 0;
    int completed = 0;

    auth_verify_t payload = {0};

    chttpx_validation_t fields[] = {
        cHTTPX_StringField("email", &payload.email, true, 5, 254, CHTTPX_TRIM | CHTTPX_LOWERCASE, validate_email),
        chttpx_validation_integer("code", &payload.code, true),
    };

    user_core_t* user = NULL;

    if (!bind_json_i18n(req, res, fields, CHTTPX_ARRAY_LEN(fields)))
        return;

    int consumed = redis_verifycode_consume(payload.email, payload.code, &code_ttl);
    if (consumed < 0)
    {
        *res = cHTTPX_ResError(cHTTPX_StatusInternalServerError, cHTTPX_i18n_t("error.something-went-wrong", req->language));
        goto cleanup;
    }

    if (consumed == 0)
    {
        *res = cHTTPX_ResError(cHTTPX_StatusBadRequest, cHTTPX_i18n_t("error.invalid-confirmation-code", req->language));
        goto cleanup;
    }

    code_held = 1;
    if (code_ttl <= 0)
        code_ttl = REDIS_VERIFYCODE_TTL;

    db_result_t lookup = db_user_core_lookup_by_email(app_context->db, payload.email, &user);
    if (db_reply_result(req, res, lookup))
        goto cleanup;

    if (user && !user->email_confirm)
    {
        db_result_t confirm_result = db_user_core_upd_email_confirm(app_context->db, true, payload.email);
        if (db_reply_result(req, res, confirm_result))
            goto cleanup;

        if (redis_user_info_delete(user->user_uid) != 0)
            logger_warn("auth_verify_handler_v2 req={%s}: failed to drop cached user", req->request_id);
    }

    if (!user)
    {
        redis_auth_token_status_t token_status =
            redis_auth_token_create(REDIS_AUTH_TOKEN_REGISTRATION, payload.email, flow_token, sizeof(flow_token));
        if (token_status != REDIS_AUTH_TOKEN_OK)
        {
            logger_error("auth_verify_handler_v2 req={%s}: failed to create registration token", req->request_id);
            *res = cHTTPX_ResError(cHTTPX_StatusInternalServerError, cHTTPX_i18n_t("error.something-went-wrong", req->language));
            goto cleanup;
        }

        completed = auth_reply_next(req, res, "registration", "registration_token", flow_token, REDIS_AUTH_TOKEN_TTL);
        goto cleanup;
    }

    if (!user_has_password(user))
    {
        session_t session = {.user_uid = user->user_uid, .expires_at = time(NULL) + REDIS_SESSION_TTL};

        session_id = redis_session_create(&session);
        if (!session_id)
        {
            *res = cHTTPX_ResError(cHTTPX_StatusInternalServerError, cHTTPX_i18n_t("error.session-creation-error", req->language));
            goto cleanup;
        }

        completed = auth_reply_next(req, res, "authenticated", "session_id", session_id, REDIS_SESSION_TTL);
        goto cleanup;
    }

    redis_auth_token_status_t token_status = redis_auth_token_create(REDIS_AUTH_TOKEN_LOGIN, payload.email, flow_token, sizeof(flow_token));
    if (token_status != REDIS_AUTH_TOKEN_OK)
    {
        logger_error("auth_verify_handler_v2 req={%s}: failed to create login token", req->request_id);
        *res = cHTTPX_ResError(cHTTPX_StatusInternalServerError, cHTTPX_i18n_t("error.something-went-wrong", req->language));
        goto cleanup;
    }

    completed = auth_reply_next(req, res, "password", "login_token", flow_token, REDIS_AUTH_TOKEN_TTL);

cleanup:
    if (code_held && !completed)
    {
        if (redis_verifycode_restore(payload.email, payload.code, code_ttl) != 0)
            logger_error("auth_verify_handler_v2 req={%s}: failed to restore confirmation code", req->request_id);
    }

    if (user)
    {
        free(user->email);
        free(user->password);
        free(user);
    }

    if (session_id)
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

    db_result_t user_del_db_result = db_user_del_by_uid(app_context->db, ctx->user->user_uid);
    if (db_reply_result(req, res, user_del_db_result))
        return;

    if (redis_user_info_delete(ctx->user->user_uid) != 0 || redis_user_block_delete(ctx->user->user_uid) != 0)
        logger_warn("auth_delete_handler_v2 req={%s}: failed to drop cached user", req->request_id);

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
        *res = cHTTPX_ResError(cHTTPX_StatusConflict, cHTTPX_i18n_t("error.s3-cloud", req->language));
        return;
    }

    *res = cHTTPX_ResMessage(cHTTPX_StatusOK, cHTTPX_i18n_t("user-deleted", req->language));
}
