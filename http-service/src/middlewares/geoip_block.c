#include "middlewarex.h"

#include "httpx.h"

#include <stdlib.h>
#include <string.h>

#include <maxminddb.h>

#include <libchttpx/libchttpx.h>

chttpx_middleware_result_t geoip_block_middleware(chttpx_request_t* req, chttpx_response_t* res)
{
    const char* env_type = getenv("TYPE");
    if (!env_type || strcmp(env_type, "PROD") != 0)
        return next;

    const char* ip = cHTTPX_ClientIP(req);
    if (!ip || ip[0] == '\0')
        return next;

    int gai_error, mmdb_error;

    MMDB_lookup_result_s result = MMDB_lookup_string(&http_server->geoip, ip, &gai_error, &mmdb_error);
    if (gai_error != 0 || mmdb_error != MMDB_SUCCESS || !result.found_entry)
    {
        return next;
    }

    MMDB_entry_data_s entry_data;

    int status = MMDB_get_value(&result.entry, &entry_data, "country", "iso_code", NULL);

    if (status != MMDB_SUCCESS || !entry_data.has_data || entry_data.type != MMDB_DATA_TYPE_UTF8_STRING || !entry_data.utf8_string)
    {
        return next;
    }

    if (strncmp(entry_data.utf8_string, "RU", 2) != 0)
    {
        const char* language = req->language[0] ? req->language : LANGUAGE_BASE;
        *res = cHTTPX_ResError(cHTTPX_StatusForbidden, cHTTPX_i18n_t("error.location-allowed", language));
        return out;
    }

    return next;
}
