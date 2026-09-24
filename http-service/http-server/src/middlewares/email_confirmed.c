#include "middlewarex.h"

#include "handlers.h"
#include "postgres/postgres_users.h"

#include <libchttpx/libchttpx.h>

chttpx_middleware_result_t email_confirmed_middleware(chttpx_request_t* req, chttpx_response_t* res)
{
    auth_token_t* ctx = cHTTPX_ContextGet(req, AUTH_CONTEXT_NAME);

    if (!ctx || !ctx->user || !ctx->user->email_confirm)
    {
        *res = cHTTPX_ResError(cHTTPX_StatusForbidden, cHTTPX_i18n_t("error.confirm-email", req->language));
        return out;
    }

    return next;
}
