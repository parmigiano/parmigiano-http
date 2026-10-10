#include "postgres/validate.h"

int db_reply_result(chttpx_request_t* req, chttpx_response_t* res, db_result_t result)
{
    switch (result)
    {
    case DB_OK:
        return 0;

    case DB_TIMEOUT:
        *res = cHTTPX_ResError(cHTTPX_StatusConnectionTimedOut, cHTTPX_i18n_t("error.database-connection-timeout", req->language));
        return 1;

    case DB_DUPLICATE:
        *res = cHTTPX_ResError(cHTTPX_StatusBadRequest, cHTTPX_i18n_t("error.repeating-data-request", req->language));
        return 1;

    case DB_ERROR:
        *res = cHTTPX_ResError(cHTTPX_StatusInternalServerError, cHTTPX_i18n_t("error.perform-database-operation", req->language));
        return 1;
    }

    return 1;
}
