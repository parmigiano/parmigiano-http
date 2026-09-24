#ifndef HANDLERS_H
#define HANDLERS_H

#include "postgres/postgres_users.h"

#include <libchttpx/libchttpx.h>

#define AUTH_CONTEXT_NAME "auth"

typedef struct
{
    user_info_t* user;
} auth_token_t;

/* Handler helper */
bool bind_json_i18n(chttpx_request_t* req, chttpx_response_t* res, chttpx_validation_t* fields, size_t field_count);

/* Docs handlers */
void swagger_json_handler_v2(chttpx_request_t* req, chttpx_response_t* res);
void swagger_gui_handler_v2(chttpx_request_t* req, chttpx_response_t* res);

/* Auth handlers */
void auth_login_handler_v2(chttpx_request_t *req, chttpx_response_t *res);
void auth_logout_handler_v2(chttpx_request_t* req, chttpx_response_t* res);
void auth_create_handler_v2(chttpx_request_t *req, chttpx_response_t *res);
void auth_confirm_email_handler_v2(chttpx_request_t* req, chttpx_response_t* res);
void auth_verify_handler_v2(chttpx_request_t* req, chttpx_response_t *res);
void auth_delete_handler_v2(chttpx_request_t* req, chttpx_response_t* res);

#endif
