#include "httpx.h"
#include "middlewarex.h"

#include "handlers.h"
#include "redis/redis_session.h"
#include "postgres/postgres_users.h"

#include <string.h>
#include <libchttpx/libchttpx.h>

chttpx_middleware_result_t authenticate_middleware(chttpx_request_t* req, chttpx_response_t* res)
{
    /* Free routes */
    if (strstr(req->path, "auth/login") != NULL || strstr(req->path, "auth/create") != NULL || strstr(req->path, "auth/verify") != NULL ||
        strstr(req->path, "auth/confirm/email") != NULL || strstr(req->path, "doc.api/swagger") != NULL)
    {
        return next;
    }

    auth_token_t* ctx = (auth_token_t*)req->context;
    if (!ctx)
    {
        *res = cHTTPX_ResJson(cHTTPX_StatusInternalServerError, "{\"error\": \"context initialization failed\"}");
        return out;
    }

    if (!ctx->lang)
    {
        ctx->lang = strdup(LANGUAGE_BASE);
    }

    const char* session_id = NULL;

    const char* authenticated_header = cHTTPX_HeaderGet(req, "Authorization");
    if (authenticated_header && strncasecmp(authenticated_header, "Bearer ", 7) == 0)
    {
        session_id = authenticated_header + 7;
    }

    if (!session_id)
    {
        *res = cHTTPX_ResJson(cHTTPX_StatusUnauthorized, "{\"message\": \"%s\"}", cHTTPX_i18n_t("error.connect-to-account", ctx->lang));
        return out;
    }

    session_t* session = redis_session_get(session_id);
    if (!session)
    {
        *res = cHTTPX_ResJson(cHTTPX_StatusUnauthorized, "{\"message\": \"%s\"}", cHTTPX_i18n_t("error.connect-to-account", ctx->lang));
        return out;
    }

    user_info_t* user = db_user_info_get_by_uid(http_server->conn, session->user_uid);

    free(session);
    session = NULL;

    if (!user)
    {
        *res = cHTTPX_ResJson(cHTTPX_StatusUnauthorized, "{\"message\": \"%s\"}", cHTTPX_i18n_t("error.connect-to-account", ctx->lang));
        return out;
    }

    ctx->user = user;

    redis_session_refresh(session_id);
    return next;
}