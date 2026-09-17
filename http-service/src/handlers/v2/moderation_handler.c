#include "handlers.h"

#include "logger.h"
#include "utilities.h"

typedef struct {
	char* target_type;
	int target_id;
} moderation_wtype_t;

void moderation_wtype_handler_v2(chttpx_request_t* req, chttpx_response_t* res)
{
	auth_token_t* ctx = cHTTPX_ContextGet(req, AUTH_CONTEXT_NAME);

	if (!ctx || !ctx->user)
    {
        *res = cHTTPX_ResError(cHTTPX_StatusUnauthorized, cHTTPX_i18n_t("error.connect-to-account", req->language));
        return;
    }

	moderation_wtype_t payload = {0};

	chttpx_validation_t fields[] = {
		cHTTPX_StringField("target_type", &payload.target_type, true, 1, 254, CHTTPX_TRIM | CHTTPX_LOWERCASE, NULL),
		chttpx_validation_integer("target_id", &payload.target_id, true),
	};

	if (!bind_json_i18n(req, res, fields, CHTTPX_ARRAY_LEN(fields)))
        return;

    *res = cHTTPX_ResNoContent();
}

rmq_action_t moderation_data_handler_v2(const rmq_message_t *message, void *userdata)
{
	(void)userdata;

	logger_error(
        "moderation_images_handler_v2: processing not implemented; "
        "message size=%zu; task will be requeued",
        message->body.size
    );

	return RMQ_REQUEUE;
}
