#include "postgres/validate.h"

#include <stdio.h>

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
    {
        const char* field = db_last_constraint();
        if (!field || !field[0])
        {
            *res = cHTTPX_ResError(cHTTPX_StatusBadRequest, cHTTPX_i18n_t("error.repeating-data-request", req->language));
            return 1;
        }

        char msg[192];
        snprintf(msg, sizeof(msg), cHTTPX_i18n_t("error.field-taken", req->language), field);
        *res = cHTTPX_ResError(cHTTPX_StatusBadRequest, msg);
        return 1;
    }

    case DB_ERROR:
        *res = cHTTPX_ResError(cHTTPX_StatusInternalServerError, cHTTPX_i18n_t("error.perform-database-operation", req->language));
        return 1;
    }

    return 1;
}
