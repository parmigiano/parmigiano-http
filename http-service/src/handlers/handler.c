#include "handlers.h"

bool bind_json_i18n(chttpx_request_t* req, chttpx_response_t* res, chttpx_validation_t* fields, size_t field_count)
{
    if (!req || !res || !fields)
        return false;

    req->error_msg[0] = '\0';

    if (!cHTTPX_Parse(req, fields, field_count) || !cHTTPX_Validate(req, fields, field_count, req->language))
    {
        const char* error = req->error_msg[0] ? req->error_msg : "invalid request body";

        *res = cHTTPX_ResError(cHTTPX_StatusBadRequest, cHTTPX_i18n_t(error, req->language));

        return false;
    }

    return true;
}
