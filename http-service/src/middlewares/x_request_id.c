#include "middlewarex.h"

#include "logger.h"
#include "handlers.h"

#include <uuid/uuid.h>

chttpx_middleware_result_t x_request_id_middleware(chttpx_request_t* req, chttpx_response_t* res)
{
    const char* x_req_id_header = cHTTPX_HeaderGet(req, "X-Request-ID");

    char str_uuid[37];

    if (!x_req_id_header)
    {
        uuid_t uid;
        uuid_generate_random(uid);
        uuid_unparse(uid, str_uuid);

        x_req_id_header = str_uuid;
    }

    if (cHTTPX_HeaderSet(req, "X-Request-ID", x_req_id_header) != 0)
    {
        logger_error("x_request_id_middleware req={%s}: failed to set header for 'X-Request-ID'", x_req_id_header);
    }

    auth_token_t* ctx = (auth_token_t*)req->context;
    if (!ctx)
        return next;

    if (!ctx->lang)
    {
        ctx->lang = strdup(LANGUAGE_BASE);
    }
    ctx->x_req_id = strdup(x_req_id_header);

    return next;
}