#include "httpx.h"
#include "middlewarex.h"

#include "handlers.h"
#include "redis/redis_session.h"
#include "redis/redis_user_cache.h"
#include "postgres/postgres_users.h"
#include "postgres/postgres_user_blocks.h"

#include <stdlib.h>
#include <libchttpx/libchttpx.h>

chttpx_middleware_result_t authenticate_middleware(chttpx_request_t* req, chttpx_response_t* res)
{
    const char* session_id = cHTTPX_BearerToken(req);

    if (!session_id)
    {
        *res = cHTTPX_ResError(cHTTPX_StatusUnauthorized, cHTTPX_i18n_t("error.connect-to-account", req->language));
        return out;
    }

    session_t* session = redis_session_get(session_id);
    if (!session)
    {
        *res = cHTTPX_ResError(cHTTPX_StatusUnauthorized, cHTTPX_i18n_t("error.connect-to-account", req->language));
        return out;
    }

    uint64_t user_uid = session->user_uid;

    free(session);
    session = NULL;

    user_info_t* user = NULL;
    if (redis_user_info_get(user_uid, &user) != 1)
    {
        user = db_user_info_get_by_uid(app_context->db, user_uid);
        if (!user)
        {
            *res = cHTTPX_ResError(cHTTPX_StatusUnauthorized, cHTTPX_i18n_t("error.connect-to-account", req->language));
            return out;
        }

        redis_user_info_set(user);
    }

    int blocked = 0;
    if (redis_user_block_get(user_uid, &blocked) != 1)
    {
        user_block_t* block = NULL;

        db_result_t block_result = db_user_block_get_active(app_context->db, user_uid, &block);
        if (block_result != DB_OK)
        {
            db_user_info_free(user);

            *res = cHTTPX_ResError(cHTTPX_StatusInternalServerError, cHTTPX_i18n_t("error.something-went-wrong", req->language));
            return out;
        }

        blocked = block != NULL;
        
        redis_user_block_set(user_uid, block);
        db_user_block_free(block);
    }

    if (blocked)
    {
        db_user_info_free(user);

        /* Do not refresh a blocked user's session TTL. */
        *res = cHTTPX_ResError(cHTTPX_StatusForbidden, "account is blocked");
        return out;
    }

    if (cHTTPX_Defer(req, user, (chttpx_cleanup_fn)db_user_info_free) != 0)
    {
        db_user_info_free(user);
        *res = cHTTPX_ResError(cHTTPX_StatusInternalServerError, cHTTPX_i18n_t("error.something-went-wrong", req->language));
        return out;
    }

    auth_token_t* ctx = cHTTPX_Alloc(req, sizeof(*ctx));
    if (!ctx)
    {
        *res = cHTTPX_ResError(cHTTPX_StatusInternalServerError, cHTTPX_i18n_t("error.something-went-wrong", req->language));
        return out;
    }

    ctx->user = user;

    if (cHTTPX_ContextSet(req, AUTH_CONTEXT_NAME, ctx, NULL) != 0)
    {
        *res = cHTTPX_ResError(cHTTPX_StatusInternalServerError, cHTTPX_i18n_t("error.something-went-wrong", req->language));
        return out;
    }

    redis_session_refresh(session_id);
    return next;
}
