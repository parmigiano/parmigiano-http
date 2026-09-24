#define _POSIX_C_SOURCE 200809L

#include "profanity.h"

#include "logger.h"

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <curl/curl.h>
#include <cjson/cJSON.h>

#define PROFANITY_MAX_RESPONSE_BYTES (1u * 1024u * 1024u)
#define PROFANITY_MAX_URL_BYTES 8192u
#define PROFANITY_DEFAULT_TIMEOUT_MS 5000L
#define PROFANITY_DEFAULT_CONNECT_TIMEOUT_MS 2000L

typedef struct
{
    char* data;
    size_t size;
    bool too_large;
    bool out_of_memory;
} response_buffer_t;

const char* profanity_result_locale_key(profanity_result_t result)
{
    switch (result)
    {
    case PROFANITY_OK:
        return "profanity.ok";
    case PROFANITY_REJECTED:
        return "profanity.rejected";
    case PROFANITY_INVALID_ARGUMENT:
        return "profanity.invalid-argument";
    case PROFANITY_NOT_CONFIGURED:
        return "profanity.not-configured";
    case PROFANITY_INVALID_URL:
        return "profanity.invalid-url";
    case PROFANITY_BODY_TOO_LARGE:
        return "profanity.body-too-large";
    case PROFANITY_OUT_OF_MEMORY:
        return "profanity.out-of-memory";
    case PROFANITY_CLIENT_ERROR:
        return "profanity.client-error";
    case PROFANITY_DNS_ERROR:
        return "profanity.dns-error";
    case PROFANITY_CONNECTION_ERROR:
        return "profanity.connection-error";
    case PROFANITY_TIMEOUT:
        return "profanity.timeout";
    case PROFANITY_TLS_ERROR:
        return "profanity.tls-error";
    case PROFANITY_NETWORK_ERROR:
        return "profanity.network-error";
    case PROFANITY_ACCESS_DENIED:
        return "profanity.access-denied";
    case PROFANITY_ENDPOINT_NOT_FOUND:
        return "profanity.endpoint-not-found";
    case PROFANITY_RATE_LIMITED:
        return "profanity.rate-limited";
    case PROFANITY_SERVICE_UNAVAILABLE:
        return "profanity.service-unavailable";
    case PROFANITY_SERVICE_ERROR:
        return "profanity.service-error";
    case PROFANITY_HTTP_ERROR:
        return "profanity.http-error";
    case PROFANITY_RESPONSE_TOO_LARGE:
        return "profanity.response-too-large";
    case PROFANITY_INVALID_RESPONSE:
        return "profanity.invalid-response";
    default:
        return "profanity.unknown-error";
    }
}

static bool env_flag_dev(const char* value)
{
    if (!value)
        return false;

    value += strspn(value, " \t\r\n");
    size_t length = strlen(value);
    while (length && strchr(" \t\r\n", value[length - 1]))
        --length;

    if (length != 3)
        return false;

    return tolower((unsigned char)value[0]) == 'd' && tolower((unsigned char)value[1]) == 'e' && tolower((unsigned char)value[2]) == 'v';
}

static bool development_env(void)
{
    return env_flag_dev(getenv("TYPE")) || env_flag_dev(getenv("GO_ENV"));
}

static bool text_is_blank(const char* text)
{
    if (!text)
        return true;

    text += strspn(text, " \t\r\n");
    return *text == '\0';
}

static profanity_result_t finish(profanity_error_t* error, profanity_result_t code, long http_status, CURLcode curl_code, int system_errno, const char* detail)
{
    if (error)
    {
        memset(error, 0, sizeof(*error));
        error->code = code;
        error->http_status = http_status;
        error->curl_code = (int)curl_code;
        error->system_errno = system_errno;
        snprintf(error->detail, sizeof(error->detail), "%s; HTTP=%ld; curl=%d; errno=%d%s%s", profanity_result_locale_key(code), http_status,
                 (int)curl_code, system_errno, detail && *detail ? ": " : "", detail ? detail : "");
    }
    return code;
}

profanity_client_t profanity_client_from_env(void)
{
    return (profanity_client_t){
        .base_url = getenv("PROFANITY_URL"),
        .timeout_ms = PROFANITY_DEFAULT_TIMEOUT_MS,
        .connect_timeout_ms = PROFANITY_DEFAULT_CONNECT_TIMEOUT_MS,
    };
}

bool profanity_client_configured(const profanity_client_t* client)
{
    return client && client->base_url && client->base_url[strspn(client->base_url, " \t\r\n")] != '\0';
}

bool profanity_result_retryable(profanity_result_t result)
{
    switch (result)
    {
    case PROFANITY_DNS_ERROR:
    case PROFANITY_CONNECTION_ERROR:
    case PROFANITY_TIMEOUT:
    case PROFANITY_NETWORK_ERROR:
    case PROFANITY_RATE_LIMITED:
    case PROFANITY_SERVICE_UNAVAILABLE:
    case PROFANITY_SERVICE_ERROR:
        return true;
    default:
        return false;
    }
}

static profanity_result_t validate_client(const profanity_client_t* client)
{
    if (!client || client->timeout_ms < 0 || client->connect_timeout_ms < 0)
        return PROFANITY_INVALID_ARGUMENT;

    return profanity_client_configured(client) ? PROFANITY_OK : PROFANITY_NOT_CONFIGURED;
}

static profanity_result_t moderate_url(const char* base_url, char** out)
{
    *out = NULL;
    const char* start = base_url + strspn(base_url, " \t\r\n");
    size_t length = strlen(start);
    while (length && strchr(" \t\r\n", start[length - 1]))
        --length;
    if (!length || length > PROFANITY_MAX_URL_BYTES || strcspn(start, "?#\r\n\t ") < length)
        return PROFANITY_INVALID_URL;

    char* url = malloc(length + sizeof("/moderate"));
    if (!url)
        return PROFANITY_OUT_OF_MEMORY;

    memcpy(url, start, length);
    url[length] = '\0';

    CURLU* parsed = curl_url();
    char *scheme = NULL, *host = NULL;
    profanity_result_t result = PROFANITY_INVALID_URL;

    if (!parsed)
    {
        free(url);
        return PROFANITY_OUT_OF_MEMORY;
    }

    CURLUcode code = curl_url_set(parsed, CURLUPART_URL, url, 0);
    if (code == CURLUE_OK)
        code = curl_url_get(parsed, CURLUPART_SCHEME, &scheme, 0);
    if (code == CURLUE_OK)
        code = curl_url_get(parsed, CURLUPART_HOST, &host, 0);
    if (code == CURLUE_OUT_OF_MEMORY)
        result = PROFANITY_OUT_OF_MEMORY;
    else if (code == CURLUE_OK && host && *host && (strcmp(scheme, "http") == 0 || strcmp(scheme, "https") == 0))
    {
        while (length && url[length - 1] == '/')
            --length;
        memcpy(url + length, "/moderate", sizeof("/moderate"));
        *out = url;
        result = PROFANITY_OK;
    }

    curl_free(scheme);
    curl_free(host);
    curl_url_cleanup(parsed);

    if (result != PROFANITY_OK)
        free(url);

    return result;
}

static size_t receive_response(char* data, size_t size, size_t count, void* userdata)
{
    response_buffer_t* buffer = userdata;
    if (size && count > SIZE_MAX / size)
    {
        buffer->too_large = true;
        return 0;
    }

    size_t length = size * count;
    if (!length)
        return 0;
    if (length > PROFANITY_MAX_RESPONSE_BYTES - buffer->size)
    {
        buffer->too_large = true;
        return 0;
    }

    char* next = realloc(buffer->data, buffer->size + length + 1);
    if (!next)
    {
        buffer->out_of_memory = true;
        return 0;
    }

    buffer->data = next;
    memcpy(next + buffer->size, data, length);
    buffer->size += length;
    next[buffer->size] = '\0';
    return length;
}

static profanity_result_t transport_result(CURLcode code)
{
    switch (code)
    {
    case CURLE_OUT_OF_MEMORY:
        return PROFANITY_OUT_OF_MEMORY;
    case CURLE_URL_MALFORMAT:
    case CURLE_UNSUPPORTED_PROTOCOL:
        return PROFANITY_INVALID_URL;
    case CURLE_COULDNT_RESOLVE_HOST:
    case CURLE_COULDNT_RESOLVE_PROXY:
        return PROFANITY_DNS_ERROR;
    case CURLE_COULDNT_CONNECT:
        return PROFANITY_CONNECTION_ERROR;
    case CURLE_OPERATION_TIMEDOUT:
        return PROFANITY_TIMEOUT;
    case CURLE_SSL_CONNECT_ERROR:
    case CURLE_PEER_FAILED_VERIFICATION:
    case CURLE_SSL_CERTPROBLEM:
    case CURLE_SSL_CIPHER:
    case CURLE_SSL_CACERT_BADFILE:
    case CURLE_SSL_ISSUER_ERROR:
    case CURLE_USE_SSL_FAILED:
        return PROFANITY_TLS_ERROR;
    case CURLE_FAILED_INIT:
    case CURLE_BAD_FUNCTION_ARGUMENT:
    case CURLE_UNKNOWN_OPTION:
    case CURLE_NOT_BUILT_IN:
        return PROFANITY_CLIENT_ERROR;
    default:
        return PROFANITY_NETWORK_ERROR;
    }
}

static profanity_result_t verdict(const response_buffer_t* buffer, long status)
{
    switch (status)
    {
    case 200:
        break;
    case 400:
    case 422:
        return PROFANITY_INVALID_ARGUMENT;
    case 413:
        return PROFANITY_BODY_TOO_LARGE;
    case 401:
    case 403:
        return PROFANITY_ACCESS_DENIED;
    case 404:
        return PROFANITY_ENDPOINT_NOT_FOUND;
    case 408:
    case 504:
        return PROFANITY_TIMEOUT;
    case 429:
        return PROFANITY_RATE_LIMITED;
    case 502:
    case 503:
        return PROFANITY_SERVICE_UNAVAILABLE;
    default:
        return status >= 500 && status <= 599 ? PROFANITY_SERVICE_ERROR : PROFANITY_HTTP_ERROR;
    }

    if (!buffer->size || memchr(buffer->data, '\0', buffer->size))
        return PROFANITY_INVALID_RESPONSE;

    const char* end = NULL;
    cJSON* root = cJSON_ParseWithLengthOpts(buffer->data, buffer->size + 1, &end, 1);
    if (!root)
        return PROFANITY_INVALID_RESPONSE;

    profanity_result_t result = PROFANITY_INVALID_RESPONSE;
    if (cJSON_IsObject(root))
    {
        cJSON *item = NULL, *flag = NULL;
        unsigned matches = 0;

        cJSON_ArrayForEach(item, root)
        {
            if (item->string && strcmp(item->string, "profane") == 0)
            {
                flag = item;
                ++matches;
            }
        }

        if (matches == 1 && cJSON_IsBool(flag))
            result = cJSON_IsTrue(flag) ? PROFANITY_REJECTED : PROFANITY_OK;
    }

    cJSON_Delete(root);
    return result;
}

static char* build_request_body(const char* const* texts, size_t count, profanity_result_t* result)
{
    cJSON* root = cJSON_CreateObject();
    cJSON* array = cJSON_CreateArray();
    if (!root || !array)
    {
        cJSON_Delete(root);
        cJSON_Delete(array);
        *result = PROFANITY_OUT_OF_MEMORY;
        return NULL;
    }

    cJSON_AddItemToObject(root, "texts", array);

    size_t added = 0;
    for (size_t i = 0; i < count; ++i)
    {
        if (text_is_blank(texts[i]))
            continue;

        cJSON* item = cJSON_CreateString(texts[i]);
        if (!item)
        {
            cJSON_Delete(root);
            *result = PROFANITY_OUT_OF_MEMORY;
            return NULL;
        }

        cJSON_AddItemToArray(array, item);
        ++added;
    }

    if (!added)
    {
        cJSON_Delete(root);
        *result = PROFANITY_OK;
        return NULL;
    }

    char* body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body)
    {
        *result = PROFANITY_OUT_OF_MEMORY;
        return NULL;
    }

    if (strlen(body) > PROFANITY_MAX_BODY_BYTES)
    {
        free(body);
        *result = PROFANITY_BODY_TOO_LARGE;
        return NULL;
    }

    *result = PROFANITY_OK;
    return body;
}

profanity_result_t profanity_check(const profanity_client_t* client, const char* const* texts, size_t count, profanity_error_t* error)
{
    profanity_result_t result = validate_client(client);
    if (result != PROFANITY_OK)
        return finish(error, result, 0, CURLE_OK, 0, NULL);
    if (!texts && count)
        return finish(error, PROFANITY_INVALID_ARGUMENT, 0, CURLE_OK, 0, NULL);

    char* body = build_request_body(texts, count, &result);
    if (!body)
        return finish(error, result, 0, CURLE_OK, 0, NULL);

    char* url = NULL;
    result = moderate_url(client->base_url, &url);
    if (result != PROFANITY_OK)
    {
        free(body);
        return finish(error, result, 0, CURLE_OK, 0, NULL);
    }

    CURL* curl = curl_easy_init();
    if (!curl)
    {
        free(body);
        free(url);
        return finish(error, PROFANITY_CLIENT_ERROR, 0, CURLE_FAILED_INIT, 0, NULL);
    }

    response_buffer_t response = {0};
    struct curl_slist* headers = NULL;
    CURLcode code = CURLE_OK;
    long status = 0;
    char detail[CURL_ERROR_SIZE] = {0};

    headers = curl_slist_append(headers, "Content-Type: application/json");
    if (!headers)
    {
        result = PROFANITY_OUT_OF_MEMORY;
        goto cleanup;
    }

#define PROFANITY_TRY(call)                                                                         \
    do                                                                                              \
    {                                                                                               \
        code = (call);                                                                              \
        if (code != CURLE_OK)                                                                       \
        {                                                                                           \
            result = code == CURLE_OUT_OF_MEMORY ? PROFANITY_OUT_OF_MEMORY : PROFANITY_CLIENT_ERROR; \
            goto cleanup;                                                                           \
        }                                                                                           \
    } while (0)

    long timeout_ms = client->timeout_ms ? client->timeout_ms : PROFANITY_DEFAULT_TIMEOUT_MS;
    long connect_timeout_ms = client->connect_timeout_ms ? client->connect_timeout_ms : PROFANITY_DEFAULT_CONNECT_TIMEOUT_MS;

    PROFANITY_TRY(curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, detail));
    PROFANITY_TRY(curl_easy_setopt(curl, CURLOPT_URL, url));
    PROFANITY_TRY(curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers));
    PROFANITY_TRY(curl_easy_setopt(curl, CURLOPT_POST, 1L));
    PROFANITY_TRY(curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body));
    PROFANITY_TRY(curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(body)));
    PROFANITY_TRY(curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms));
    PROFANITY_TRY(curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, connect_timeout_ms));
    PROFANITY_TRY(curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L));
    PROFANITY_TRY(curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L));
    PROFANITY_TRY(curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L));
    PROFANITY_TRY(curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L));
    PROFANITY_TRY(curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receive_response));
    PROFANITY_TRY(curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response));
#undef PROFANITY_TRY

    code = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);

    if (response.too_large)
        result = PROFANITY_RESPONSE_TOO_LARGE;
    else if (response.out_of_memory)
        result = PROFANITY_OUT_OF_MEMORY;
    else if (code != CURLE_OK)
        result = transport_result(code);
    else
        result = verdict(&response, status);

cleanup:
    finish(error, result, status, code, 0, code != CURLE_OK ? (detail[0] ? detail : curl_easy_strerror(code)) : NULL);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    free(response.data);
    free(url);
    free(body);
    return result;
}

profanity_result_t profanity_check_from_env(const char* const* texts, size_t count, profanity_error_t* error)
{
    profanity_client_t client = profanity_client_from_env();
    if (!profanity_client_configured(&client))
    {
        if (development_env())
        {
            logger_warn("profanity: PROFANITY_URL is empty, skipping text moderation");
            return finish(error, PROFANITY_OK, 0, CURLE_OK, 0, NULL);
        }

        return finish(error, PROFANITY_NOT_CONFIGURED, 0, CURLE_OK, 0, NULL);
    }

    return profanity_check(&client, texts, count, error);
}

bool profanity_reject_from_env(chttpx_request_t* req, chttpx_response_t* res, const char* reason, const char* const* texts, size_t count)
{
    if (!req || !res)
        return false;

    profanity_error_t error = {0};
    profanity_result_t result = profanity_check_from_env(texts, count, &error);
    if (result == PROFANITY_OK)
        return true;

    const char* request_id = req->request_id ? req->request_id : "";
    const char* why = reason && *reason ? reason : "text";

    if (result == PROFANITY_REJECTED)
    {
        logger_info("profanity: rejected reason=%s req={%s}", why, request_id);
        *res = cHTTPX_ResError(cHTTPX_StatusBadRequest, cHTTPX_i18n_t("error.text-profanity", req->language));
        return false;
    }

    logger_error("profanity check failed req={%s}: %s", request_id, error.detail[0] ? error.detail : profanity_result_locale_key(result));
    *res = cHTTPX_ResError(cHTTPX_StatusServiceUnavailable, cHTTPX_i18n_t("error.text-moderation-unavailable", req->language));
    return false;
}
