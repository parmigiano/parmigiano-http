#include "routes.h"

#include "handlers.h"

void routes(void)
{
    chttpx_router_t v2 = cHTTPX_RoutePathPrefix("/api/v2");

    /* Swagger routes */
    cHTTPX_RegisterRoute(&v2, "GET", "/doc.api/swagger/json", swagger_json_handler_v2);
    cHTTPX_RegisterRoute(&v2, "GET", "/doc.api/swagger/gui", swagger_gui_handler_v2);
    /* Auth routes */
    cHTTPX_RegisterRoute(&v2, "POST", "/auth/login", auth_login_handler_v2);
    cHTTPX_RegisterRoute(&v2, "POST", "/auth/logout", auth_logout_handler_v2);
    cHTTPX_RegisterRoute(&v2, "POST", "/auth/create", auth_create_handler_v2);
    cHTTPX_RegisterRoute(&v2, "POST", "/auth/confirm/email", auth_confirm_email_handler_v2);
    cHTTPX_RegisterRoute(&v2, "POST", "/auth/verify", auth_verify_handler_v2);
    cHTTPX_RegisterRoute(&v2, "DELETE", "/auth/delete", auth_delete_handler_v2);
    /* User routes */
    cHTTPX_RegisterRoute(&v2, "GET", "/users/me", user_me_handler_v2);
    cHTTPX_RegisterRoute(&v2, "PATCH", "/users/me", user_update_profile_handler_v2);
    cHTTPX_RegisterRoute(&v2, "PATCH", "/users/me/avatar", user_upload_avatar_handler_v2);
    cHTTPX_RegisterRoute(&v2, "GET", "/users/{user_uid}", user_get_profile_handler_v2);
    /* Chat routes */
    /* /chats?offset=0 */
    cHTTPX_RegisterRoute(&v2, "GET", "/chats", chat_get_my_history_handler_v2);
    cHTTPX_RegisterRoute(&v2, "GET", "/chats/u/{username}", chat_get_by_username_handler_v2);
    cHTTPX_RegisterRoute(&v2, "GET", "/chats/{chat_id}", chat_get_settings_handler_v2);
    cHTTPX_RegisterRoute(&v2, "PATCH", "/chats/{chat_id}/cbackground", chat_upload_custom_bg_handler_v2);
    cHTTPX_RegisterRoute(&v2, "POST", "/chats/{chat_id}/translate", chat_translate_handler_v2);
    cHTTPX_RegisterRoute(&v2, "POST", "/chats/bot/ai", chat_bot_default_ai_handler_v2);
    /* Media routes */
    cHTTPX_RegisterRoute(&v2, "POST", "/media/chats/{chat_id}/upload", media_upload_in_chat_handler_v2);
    /* Group chats routes */
    cHTTPX_RegisterRoute(&v2, "POST", "/groups", group_chats_create_handler_v2);
    cHTTPX_RegisterRoute(&v2, "PUT", "/groups/{group_id}", group_chats_edit_handler_v2);
    cHTTPX_RegisterRoute(&v2, "DELETE", "/groups/{group_id}", group_chats_delete_handler_v2);
}
