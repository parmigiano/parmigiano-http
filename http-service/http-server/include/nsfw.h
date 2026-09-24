#ifndef NSFW_H
#define NSFW_H

#include <stddef.h>
#include <stdbool.h>

#define NSFW_MAX_IMAGE_BYTES (2u * 1024u * 1024u)

typedef enum {
	NSFW_OK = 0,
	NSFW_REJECTED,

    NSFW_INVALID_ARGUMENT,
    NSFW_NOT_CONFIGURED,
    NSFW_INVALID_URL,
    NSFW_EMPTY_IMAGE,
    NSFW_IMAGE_TOO_LARGE,
    NSFW_INVALID_IMAGE,
    NSFW_UNSUPPORTED_MEDIA,

    NSFW_OUT_OF_MEMORY,
    NSFW_CLIENT_ERROR,
    NSFW_DNS_ERROR,
    NSFW_CONNECTION_ERROR,
    NSFW_TIMEOUT,
    NSFW_TLS_ERROR,
    NSFW_NETWORK_ERROR,

    NSFW_ACCESS_DENIED,
    NSFW_ENDPOINT_NOT_FOUND,
    NSFW_RATE_LIMITED,
    NSFW_SERVICE_UNAVAILABLE,
    NSFW_SERVICE_ERROR,
    NSFW_HTTP_ERROR,

    NSFW_RESPONSE_TOO_LARGE,
    NSFW_INVALID_RESPONSE,
    NSFW_FILE_ERROR
} nsfw_result_t;

typedef struct {
	const char *base_url;
	long timeout_ms; 			/* 0 = 5000 */
	long connect_timeout_ms; 	/* 0 = 2000 */
} nsfw_client_t;

typedef struct {
	nsfw_result_t code;
	long http_status;
	int curl_code;
	char detail[512];
	int system_errno;
} nsfw_error_t;


nsfw_client_t nsfw_client_from_env(void);
bool nsfw_client_configured(const nsfw_client_t *client);

nsfw_result_t nsfw_check(const nsfw_client_t *client, const void *payload, size_t payload_size, const char *content_type, nsfw_error_t *error);

nsfw_result_t nsfw_check_file(const nsfw_client_t *client, const char *path, const char *content_type, nsfw_error_t *error);

nsfw_result_t nsfw_check_from_env(const void *payload, size_t payload_size, const char *content_type, nsfw_error_t *error);
nsfw_result_t nsfw_check_file_from_env(const char *path, const char *content_type, nsfw_error_t *error);

const char *nsfw_result_locale_key(nsfw_result_t result);

bool nsfw_result_retryable(nsfw_result_t result);

#endif
