#include "httpx.h"
#include "middlewarex.h"

#include "handlers.h"
#include "redis/redis_session.h"
#include "postgres/postgres_users.h"

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

    user_info_t* user = db_user_info_get_by_uid(http_server->conn, user_uid);
    if (!user)
    {
        *res = cHTTPX_ResError(cHTTPX_StatusUnauthorized, cHTTPX_i18n_t("error.connect-to-account", req->language));
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
