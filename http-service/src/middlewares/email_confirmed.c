#include "middlewarex.h"

#include "handlers.h"
#include "postgres/postgres_users.h"

#include <libchttpx/libchttpx.h>

chttpx_middleware_result_t email_confirmed_middleware(chttpx_request_t* req, chttpx_response_t* res)
{
    if (strstr(req->path, "auth/login") != NULL || strstr(req->path, "auth/create") != NULL || strstr(req->path, "auth/verify") != NULL ||
        strstr(req->path, "doc.api/swagger") != NULL)
    {
        return next;
    }

    auth_token_t* ctx = (auth_token_t*)req->context;
    if (!ctx->lang)
    {
        ctx->lang = strdup(LANGUAGE_BASE);
    }

    if (!ctx || !ctx->user || !ctx->user->email_confirm)
    {
        *res = cHTTPX_ResJson(cHTTPX_StatusForbidden, "{\"error\": \"%s\"}", cHTTPX_i18n_t("error.confirm-email", ctx->lang));
        return out;
    }

    return next;
}
