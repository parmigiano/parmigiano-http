#include "middlewarex.h"

#include "logger.h"
#include "handlers.h"
#include "postgres/postgres_users.h"

#include <stdio.h>
#include <libchttpx/libchttpx.h>

void auth_context_free(void* ptr)
{
    auth_token_t* ctx = (auth_token_t*)ptr;

    if (ctx->user)
    {
        db_user_info_free(ctx->user);
        ctx->user = NULL;
    }

    if (ctx->x_req_id)
        free(ctx->x_req_id);

    if (ctx->lang)
        free(ctx->lang);

    free(ctx);
}

chttpx_middleware_result_t language_middleware(chttpx_request_t* req, chttpx_response_t* res)
{
    const char* hal = cHTTPX_HeaderGet(req, "Accept-Language");
    char lang_code[3];

    if (!hal)
    {
        lang_code[0] = 'e';
        lang_code[1] = 'n';
        lang_code[2] = '\0';
    }
    else
    {
        lang_code[0] = hal[0];
        lang_code[1] = hal[1];
        lang_code[2] = '\0';
    }

    auth_token_t* ctx = (auth_token_t*)req->context;
    if (!ctx)
    {
        ctx = calloc(1, sizeof(auth_token_t));
        if (!ctx)
        {
            logger_error("language_middleware: calloc failed for ctx");

            fprintf(stderr, "calloc failed\n");

            *res = cHTTPX_ResJson(cHTTPX_StatusInternalServerError, "{\"error\": \"%s\"}", cHTTPX_i18n_t("error.context-initialization", lang_code));
            return out;
        }

        ctx->user = NULL;
        ctx->lang = strdup(lang_code);

        req->context = ctx;
        req->context_free = auth_context_free;
    }
    else
    {
        if (ctx->lang)
            free(ctx->lang);
        ctx->lang = strdup(lang_code);
    }

    return next;
}