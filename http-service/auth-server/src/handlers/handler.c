#include "handlers.h"

#include <stdio.h>

bool bind_json_i18n(chttpx_request_t* req, chttpx_response_t* res, chttpx_validation_t* fields, size_t field_count)
{
    if (!req || !res || !fields)
        return false;

    int bind = cHTTPX_BindJSON(req, fields, field_count);
    if (bind == cHTTPX_BIND_OK)
        return true;

    const char* field = req->error_field[0] ? req->error_field : "";
    const char* lang = req->language;

    char msg[sizeof(req->error_msg)];
    const char* out = msg;

    switch (bind)
    {
    case cHTTPX_BIND_REQUIRED:
        snprintf(msg, sizeof(msg), cHTTPX_i18n_t("error.bind-required", lang), field);
        break;
    case cHTTPX_BIND_MIN_LENGTH:
        snprintf(msg, sizeof(msg), cHTTPX_i18n_t("error.bind-min-length", lang), field, req->error_num);
        break;
    case cHTTPX_BIND_MAX_LENGTH:
        snprintf(msg, sizeof(msg), cHTTPX_i18n_t("error.bind-max-length", lang), field, req->error_num);
        break;
    case cHTTPX_BIND_INVALID_EMAIL:
        snprintf(msg, sizeof(msg), cHTTPX_i18n_t("error.bind-invalid-email", lang), field);
        break;
    case cHTTPX_BIND_GENERIC:
        if (req->error_msg[0])
            out = cHTTPX_i18n_t(req->error_msg, lang);
        else
            snprintf(msg, sizeof(msg), cHTTPX_i18n_t("error.bind-generic", lang), field);
        break;
    case cHTTPX_BIND_INVALID_JSON:
        out = cHTTPX_i18n_t("error.bind-invalid-json", lang);
        break;
    case cHTTPX_BIND_TYPE:
        out = cHTTPX_i18n_t("error.bind-type", lang);
        break;
    case cHTTPX_BIND_MEMORY:
        out = cHTTPX_i18n_t("error.bind-memory", lang);
        break;
    case cHTTPX_BIND_INVALID_ARGUMENT:
    default:
        out = cHTTPX_i18n_t("error.bind-invalid-request", lang);
        break;
    }

    *res = cHTTPX_ResError(cHTTPX_StatusBadRequest, out);
    return false;
}
