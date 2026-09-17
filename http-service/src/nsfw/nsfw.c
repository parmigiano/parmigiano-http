#define _POSIX_C_SOURCE 200809L

#include "nsfw.h"

#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/stat.h>

#include <curl/curl.h>
#include <cjson/cJSON.h>

#define NSFW_MAX_RESPONSE_BYTES (1u * 1024u * 1024u)
#define NSFW_MAX_URL_BYTES 8192u

typedef struct
{
    char* data;
    size_t size;
    bool too_large;
    bool out_of_memory;
} response_buffer_t;

const char* nsfw_result_locale_key(nsfw_result_t result)
{
    switch (result)
    {
    case NSFW_OK:
        return "nsfw.ok";
    case NSFW_REJECTED:
        return "nsfw.rejected";
    case NSFW_INVALID_ARGUMENT:
        return "nsfw.invalid-argument";
    case NSFW_NOT_CONFIGURED:
        return "nsfw.not-configured";
    case NSFW_INVALID_URL:
        return "nsfw.invalid-url";
    case NSFW_EMPTY_IMAGE:
        return "nsfw.empty-image";
    case NSFW_IMAGE_TOO_LARGE:
        return "nsfw.image-too-large";
    case NSFW_INVALID_IMAGE:
        return "nsfw.invalid-image";
    case NSFW_UNSUPPORTED_MEDIA:
        return "nsfw.unsupported-media";
    case NSFW_OUT_OF_MEMORY:
        return "nsfw.out-of-memory";
    case NSFW_CLIENT_ERROR:
        return "nsfw.client-error";
    case NSFW_DNS_ERROR:
        return "nsfw.dns-error";
    case NSFW_CONNECTION_ERROR:
        return "nsfw.connection-error";
    case NSFW_TIMEOUT:
        return "nsfw.timeout";
    case NSFW_TLS_ERROR:
        return "nsfw.tls-error";
    case NSFW_NETWORK_ERROR:
        return "nsfw.network-error";
    case NSFW_ACCESS_DENIED:
        return "nsfw.access-denied";
    case NSFW_ENDPOINT_NOT_FOUND:
        return "nsfw.endpoint-not-found";
    case NSFW_RATE_LIMITED:
        return "nsfw.rate-limited";
    case NSFW_SERVICE_UNAVAILABLE:
        return "nsfw.service-unavailable";
    case NSFW_SERVICE_ERROR:
        return "nsfw.service-error";
    case NSFW_HTTP_ERROR:
        return "nsfw.http-error";
    case NSFW_RESPONSE_TOO_LARGE:
        return "nsfw.response-too-large";
    case NSFW_INVALID_RESPONSE:
        return "nsfw.invalid-response";
    case NSFW_FILE_ERROR:
        return "nsfw.file-error";
    default:
        return "nsfw.unknown-error";
    }
}

static nsfw_result_t finish(nsfw_error_t* error, nsfw_result_t code, long http_status, CURLcode curl_code, int system_errno, const char* detail)
{
    if (error)
    {
        memset(error, 0, sizeof(*error));
        error->code = code;
        error->http_status = http_status;
        error->curl_code = (int)curl_code;
        error->system_errno = system_errno;
        snprintf(error->detail, sizeof(error->detail), "%s; HTTP=%ld; curl=%d; errno=%d%s%s", nsfw_result_locale_key(code), http_status,
                 (int)curl_code, system_errno, detail && *detail ? ": " : "", detail ? detail : "");
    }
    return code;
}

nsfw_client_t nsfw_client_from_env(void)
{
    return (nsfw_client_t){.base_url = getenv("NSFW_URL"), .timeout_ms = 5000, .connect_timeout_ms = 2000};
}

bool nsfw_client_configured(const nsfw_client_t* client)
{
    return client && client->base_url && client->base_url[strspn(client->base_url, " \t\r\n")] != '\0';
}

bool nsfw_result_retryable(nsfw_result_t result)
{
    switch (result)
    {
    case NSFW_DNS_ERROR:
    case NSFW_CONNECTION_ERROR:
    case NSFW_TIMEOUT:
    case NSFW_NETWORK_ERROR:
    case NSFW_RATE_LIMITED:
    case NSFW_SERVICE_UNAVAILABLE:
    case NSFW_SERVICE_ERROR:
        return true;
    default:
        return false;
    }
}

static nsfw_result_t validate_client(const nsfw_client_t* client)
{
    if (!client || client->timeout_ms < 0 || client->connect_timeout_ms < 0)
        return NSFW_INVALID_ARGUMENT;

    return nsfw_client_configured(client) ? NSFW_OK : NSFW_NOT_CONFIGURED;
}

static nsfw_result_t moderate_url(const char* base_url, char** out)
{
    *out = NULL;
    const char* start = base_url + strspn(base_url, " \t\r\n");
    size_t length = strlen(start);
    while (length && strchr(" \t\r\n", start[length - 1]))
        --length;
    if (!length || length > NSFW_MAX_URL_BYTES || strcspn(start, "?#\r\n\t ") < length)
        return NSFW_INVALID_URL;

    char* url = malloc(length + sizeof("/moderate"));
    if (!url)
        return NSFW_OUT_OF_MEMORY;

    memcpy(url, start, length);
    url[length] = '\0';

    CURLU* parsed = curl_url();

    char *scheme = NULL, *host = NULL;

    nsfw_result_t result = NSFW_INVALID_URL;

    if (!parsed)
    {
        free(url);
        return NSFW_OUT_OF_MEMORY;
    }

    CURLUcode code = curl_url_set(parsed, CURLUPART_URL, url, 0);
    if (code == CURLUE_OK)
        code = curl_url_get(parsed, CURLUPART_SCHEME, &scheme, 0);
    if (code == CURLUE_OK)
        code = curl_url_get(parsed, CURLUPART_HOST, &host, 0);
    if (code == CURLUE_OUT_OF_MEMORY)
        result = NSFW_OUT_OF_MEMORY;
    else if (code == CURLUE_OK && host && *host && (strcmp(scheme, "http") == 0 || strcmp(scheme, "https") == 0))
    {
        while (length && url[length - 1] == '/')
            --length;
        memcpy(url + length, "/moderate", sizeof("/moderate"));
        *out = url;
        result = NSFW_OK;
    }

    curl_free(scheme);
    curl_free(host);
    curl_url_cleanup(parsed);

    if (result != NSFW_OK)
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
    if (length > NSFW_MAX_RESPONSE_BYTES - buffer->size)
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

static nsfw_result_t transport_result(CURLcode code)
{
    switch (code)
    {
    case CURLE_OUT_OF_MEMORY:
        return NSFW_OUT_OF_MEMORY;
    case CURLE_URL_MALFORMAT:
    case CURLE_UNSUPPORTED_PROTOCOL:
        return NSFW_INVALID_URL;
    case CURLE_COULDNT_RESOLVE_HOST:
    case CURLE_COULDNT_RESOLVE_PROXY:
        return NSFW_DNS_ERROR;
    case CURLE_COULDNT_CONNECT:
        return NSFW_CONNECTION_ERROR;
    case CURLE_OPERATION_TIMEDOUT:
        return NSFW_TIMEOUT;
    case CURLE_SSL_CONNECT_ERROR:
    case CURLE_PEER_FAILED_VERIFICATION:
    case CURLE_SSL_CERTPROBLEM:
    case CURLE_SSL_CIPHER:
    case CURLE_SSL_CACERT_BADFILE:
    case CURLE_SSL_ISSUER_ERROR:
    case CURLE_USE_SSL_FAILED:
        return NSFW_TLS_ERROR;
    case CURLE_FAILED_INIT:
    case CURLE_BAD_FUNCTION_ARGUMENT:
    case CURLE_UNKNOWN_OPTION:
    case CURLE_NOT_BUILT_IN:
        return NSFW_CLIENT_ERROR;
    default:
        return NSFW_NETWORK_ERROR;
    }
}

static nsfw_result_t verdict(const response_buffer_t* buffer, long status)
{
    switch (status)
    {
    case 200:
        break;
    case 400:
    case 422:
        return NSFW_INVALID_IMAGE;
    case 413:
        return NSFW_IMAGE_TOO_LARGE;
    case 415:
        return NSFW_UNSUPPORTED_MEDIA;
    case 401:
    case 403:
        return NSFW_ACCESS_DENIED;
    case 404:
        return NSFW_ENDPOINT_NOT_FOUND;
    case 408:
    case 504:
        return NSFW_TIMEOUT;
    case 429:
        return NSFW_RATE_LIMITED;
    case 502:
    case 503:
        return NSFW_SERVICE_UNAVAILABLE;
    default:
        return status >= 500 && status <= 599 ? NSFW_SERVICE_ERROR : NSFW_HTTP_ERROR;
    }

    if (!buffer->size || memchr(buffer->data, '\0', buffer->size))
        return NSFW_INVALID_RESPONSE;

    const char* end = NULL;

    cJSON* root = cJSON_ParseWithLengthOpts(buffer->data, buffer->size + 1, &end, 1);
    if (!root)
        return NSFW_INVALID_RESPONSE;

    nsfw_result_t result = NSFW_INVALID_RESPONSE;
    if (cJSON_IsObject(root))
    {
        cJSON *item = NULL, *flag = NULL;
        unsigned matches = 0;

        cJSON_ArrayForEach(item, root)
        {
            if (item->string && strcmp(item->string, "nsfw") == 0)
            {
                flag = item;
                ++matches;
            }
        }

        if (matches == 1 && cJSON_IsBool(flag))
            result = cJSON_IsTrue(flag) ? NSFW_REJECTED : NSFW_OK;
    }

    cJSON_Delete(root);
    return result;
}

nsfw_result_t nsfw_check(const nsfw_client_t* client, const void* payload, size_t payload_size, const char* content_type, nsfw_error_t* error)
{
    nsfw_result_t result = validate_client(client);
    if (result != NSFW_OK)
        return finish(error, result, 0, CURLE_OK, 0, NULL);
    if (!payload_size)
        return finish(error, NSFW_EMPTY_IMAGE, 0, CURLE_OK, 0, NULL);
    if (!payload || (content_type && strpbrk(content_type, "\r\n")))
        return finish(error, NSFW_INVALID_ARGUMENT, 0, CURLE_OK, 0, NULL);
    if (payload_size > NSFW_MAX_IMAGE_BYTES)
        return finish(error, NSFW_IMAGE_TOO_LARGE, 0, CURLE_OK, 0, NULL);

    char* url = NULL;
    result = moderate_url(client->base_url, &url);
    if (result != NSFW_OK)
        return finish(error, result, 0, CURLE_OK, 0, NULL);

    CURL* curl = curl_easy_init();
    if (!curl)
    {
        free(url);
        return finish(error, NSFW_CLIENT_ERROR, 0, CURLE_FAILED_INIT, 0, NULL);
    }

    response_buffer_t response = {0};

    curl_mime* mime = curl_mime_init(curl);
    CURLcode code = CURLE_OK;

    long status = 0;
    char detail[CURL_ERROR_SIZE] = {0};

    if (!mime)
    {
        result = NSFW_OUT_OF_MEMORY;
        goto cleanup;
    }

    curl_mimepart* part = curl_mime_addpart(mime);
    if (!part)
    {
        result = NSFW_OUT_OF_MEMORY;
        goto cleanup;
    }

#define NSFW_TRY(call)                                                                                                                               \
    do                                                                                                                                               \
    {                                                                                                                                                \
        code = (call);                                                                                                                               \
        if (code != CURLE_OK)                                                                                                                        \
        {                                                                                                                                            \
            result = code == CURLE_OUT_OF_MEMORY ? NSFW_OUT_OF_MEMORY : NSFW_CLIENT_ERROR;                                                           \
            goto cleanup;                                                                                                                            \
        }                                                                                                                                            \
    } while (0)

    NSFW_TRY(curl_mime_name(part, "image"));
    NSFW_TRY(curl_mime_filename(part, "image.bin"));
    NSFW_TRY(curl_mime_type(part, content_type && *content_type ? content_type : "application/octet-stream"));
    NSFW_TRY(curl_mime_data(part, payload, payload_size));
    NSFW_TRY(curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, detail));
    NSFW_TRY(curl_easy_setopt(curl, CURLOPT_URL, url));
    NSFW_TRY(curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime));
    NSFW_TRY(curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, client->timeout_ms ? client->timeout_ms : 5000L));
    NSFW_TRY(curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, client->connect_timeout_ms ? client->connect_timeout_ms : 2000L));
    NSFW_TRY(curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L));
    NSFW_TRY(curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L));
    NSFW_TRY(curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L));
    NSFW_TRY(curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L));
    NSFW_TRY(curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receive_response));
    NSFW_TRY(curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response));
#undef NSFW_TRY

    code = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);

    if (response.too_large)
        result = NSFW_RESPONSE_TOO_LARGE;
    else if (response.out_of_memory)
        result = NSFW_OUT_OF_MEMORY;
    else if (code != CURLE_OK)
        result = transport_result(code);
    else
        result = verdict(&response, status);

cleanup:
    finish(error, result, status, code, 0, code != CURLE_OK ? (detail[0] ? detail : curl_easy_strerror(code)) : NULL);
    curl_easy_cleanup(curl);
    if (mime)
        curl_mime_free(mime);
    free(response.data);
    free(url);
    return result;
}

nsfw_result_t nsfw_check_file(const nsfw_client_t* client, const char* path, const char* content_type, nsfw_error_t* error)
{
    nsfw_result_t result = validate_client(client);
    if (result != NSFW_OK)
        return finish(error, result, 0, CURLE_OK, 0, NULL);

    if (!path || !*path || (content_type && strpbrk(content_type, "\r\n")))
        return finish(error, NSFW_INVALID_ARGUMENT, 0, CURLE_OK, 0, NULL);

    int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0)
        return finish(error, NSFW_FILE_ERROR, 0, CURLE_OK, errno, "open failed");

    struct stat info;
    int saved_errno = 0;
    unsigned char* data = NULL;

    if (fstat(fd, &info) != 0)
    {
        saved_errno = errno;
        result = NSFW_FILE_ERROR;
        goto cleanup;
    }

    if (!S_ISREG(info.st_mode))
    {
        result = NSFW_FILE_ERROR;
        goto cleanup;
    }

    if (info.st_size > (off_t)NSFW_MAX_IMAGE_BYTES)
    {
        result = NSFW_IMAGE_TOO_LARGE;
        goto cleanup;
    }

    data = malloc(NSFW_MAX_IMAGE_BYTES + 1u);
    if (!data)
    {
        result = NSFW_OUT_OF_MEMORY;
        goto cleanup;
    }

    size_t used = 0;
    while (used < NSFW_MAX_IMAGE_BYTES + 1u)
    {
        ssize_t count = read(fd, data + used, NSFW_MAX_IMAGE_BYTES + 1u - used);
        if (count < 0)
        {
            if (errno == EINTR)
                continue;

            saved_errno = errno;
            result = NSFW_FILE_ERROR;

            goto cleanup;
        }
		
        if (!count)
            break;

        used += (size_t)count;
    }

    close(fd);
    result = nsfw_check(client, data, used, content_type, error);
    free(data);

    return result;
cleanup:
    close(fd);
    free(data);
    return finish(error, result, 0, CURLE_OK, saved_errno, result == NSFW_FILE_ERROR ? "cannot read a regular image file" : NULL);
}

nsfw_result_t nsfw_check_from_env(const void* payload, size_t payload_size, const char* content_type, nsfw_error_t* error)
{
    nsfw_client_t client = nsfw_client_from_env();
    return nsfw_check(&client, payload, payload_size, content_type, error);
}

nsfw_result_t nsfw_check_file_from_env(const char* path, const char* content_type, nsfw_error_t* error)
{
    nsfw_client_t client = nsfw_client_from_env();
    return nsfw_check_file(&client, path, content_type, error);
}
