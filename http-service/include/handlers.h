#ifndef HANDLERS_H
#define HANDLERS_H

#include "rabbitmq.h"
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

/* User handlers */
void user_me_handler_v2(chttpx_request_t* req, chttpx_response_t* res);
void user_upload_avatar_handler_v2(chttpx_request_t *req, chttpx_response_t *res);
void user_get_profile_handler_v2(chttpx_request_t *req, chttpx_response_t *res);
void user_update_profile_handler_v2(chttpx_request_t *req, chttpx_response_t *res);

/* Chat handlers */
void chat_get_my_history_handler_v2(chttpx_request_t* req, chttpx_response_t* res);
void chat_get_by_username_handler_v2(chttpx_request_t *req, chttpx_response_t *res);
void chat_get_settings_handler_v2(chttpx_request_t* req, chttpx_response_t* res);
void chat_upload_custom_bg_handler_v2(chttpx_request_t* req, chttpx_response_t* res);
void chat_translate_handler_v2(chttpx_request_t* req, chttpx_response_t* res);
void chat_bot_default_ai_handler_v2(chttpx_request_t* req, chttpx_response_t* res);

/* Media handlers */
void media_upload_in_chat_handler_v2(chttpx_request_t* req, chttpx_response_t* res);

/* Group handlers */
void group_chats_create_handler_v2(chttpx_request_t* req, chttpx_response_t* res);
void group_chats_edit_handler_v2(chttpx_request_t* req, chttpx_response_t* res);
void group_chats_delete_handler_v2(chttpx_request_t* req, chttpx_response_t* res);

/* Moderation handlers */
rmq_action_t moderation_data_handler_v2(const rmq_message_t *message, void *userdata);

#endif
