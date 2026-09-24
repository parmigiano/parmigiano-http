#ifndef PROFANITY_H
#define PROFANITY_H

#include <stddef.h>
#include <stdbool.h>

#include <libchttpx/libchttpx.h>

#define PROFANITY_MAX_BODY_BYTES (64u * 1024u)

typedef enum {
	PROFANITY_OK = 0,
	PROFANITY_REJECTED,

	PROFANITY_INVALID_ARGUMENT,
	PROFANITY_NOT_CONFIGURED,
	PROFANITY_INVALID_URL,
	PROFANITY_BODY_TOO_LARGE,

	PROFANITY_OUT_OF_MEMORY,
	PROFANITY_CLIENT_ERROR,
	PROFANITY_DNS_ERROR,
	PROFANITY_CONNECTION_ERROR,
	PROFANITY_TIMEOUT,
	PROFANITY_TLS_ERROR,
	PROFANITY_NETWORK_ERROR,

	PROFANITY_ACCESS_DENIED,
	PROFANITY_ENDPOINT_NOT_FOUND,
	PROFANITY_RATE_LIMITED,
	PROFANITY_SERVICE_UNAVAILABLE,
	PROFANITY_SERVICE_ERROR,
	PROFANITY_HTTP_ERROR,

	PROFANITY_RESPONSE_TOO_LARGE,
	PROFANITY_INVALID_RESPONSE
} profanity_result_t;

typedef struct {
	const char *base_url;
	long timeout_ms; 			/* 0 = 5000 */
	long connect_timeout_ms; 	/* 0 = 2000 */
} profanity_client_t;

typedef struct {
	profanity_result_t code;
	long http_status;
	int curl_code;
	char detail[512];
	int system_errno;
} profanity_error_t;

profanity_client_t profanity_client_from_env(void);
bool profanity_client_configured(const profanity_client_t *client);

profanity_result_t profanity_check(const profanity_client_t *client, const char *const *texts, size_t count, profanity_error_t *error);
profanity_result_t profanity_check_from_env(const char *const *texts, size_t count, profanity_error_t *error);

const char *profanity_result_locale_key(profanity_result_t result);
bool profanity_result_retryable(profanity_result_t result);

/* Writes *res and returns false when texts are rejected or the sidecar is unavailable. */
bool profanity_reject_from_env(chttpx_request_t *req, chttpx_response_t *res, const char *reason, const char *const *texts, size_t count);

#endif
