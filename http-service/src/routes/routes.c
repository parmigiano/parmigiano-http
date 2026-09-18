#include "routes.h"

#include "handlers.h"
#include "middlewarex.h"

const rabbitmq_route_t* rabbitmq_routes(size_t* count)
{
    static const rabbitmq_route_t items[] = {
        {MODERATION_QUEUE, moderation_data_handler_v2},
    };

    if (count)
        *count = sizeof(items) / sizeof(items[0]);

    return items;
}

void http_routes(chttpx_serv_t* server)
{
    chttpx_router_t api = cHTTPX_RoutePathPrefix(server, "/api/v2");

    static const char* image_types[] = {cHTTPX_CTYPE_JPEG, cHTTPX_CTYPE_PNG, cHTTPX_CTYPE_GIF};
    static const char* background_types[] = {cHTTPX_CTYPE_JPEG, cHTTPX_CTYPE_PNG};

	/* Policy */
    static const chttpx_upload_policy_t avatar_policy = {
        .max_size = 10 * 1024 * 1024,
        .allowed_types = image_types,
        .allowed_types_count = CHTTPX_ARRAY_LEN(image_types),
    };
    static const chttpx_upload_policy_t background_policy = {
        .max_size = 25 * 1024 * 1024,
        .allowed_types = background_types,
        .allowed_types_count = CHTTPX_ARRAY_LEN(background_types),
    };
    static const chttpx_upload_policy_t media_policy = {
        .max_size = 500ULL * 1024 * 1024,
    };

    /* Swagger routes */
    chttpx_router_t doc = cHTTPX_RouteGroup(&api, "/doc.api");

    cHTTPX_Get(&doc, "/swagger/json", swagger_json_handler_v2);
    cHTTPX_Get(&doc, "/swagger/gui", swagger_gui_handler_v2);

    /* Auth routes */
    chttpx_router_t auth = cHTTPX_RouteGroup(&api, "/auth");

    cHTTPX_Post(&auth, "/login", auth_login_handler_v2);
    cHTTPX_Post(&auth, "/create", auth_create_handler_v2);
    cHTTPX_Post(&auth, "/confirm/email", auth_confirm_email_handler_v2);
    cHTTPX_Post(&auth, "/verify", auth_verify_handler_v2);

    chttpx_route_t* auth_logout_route = cHTTPX_Post(&auth, "/logout", auth_logout_handler_v2);
    cHTTPX_RouteUse(auth_logout_route, authenticate_middleware);
    chttpx_route_t* auth_delete_account_route = cHTTPX_Delete(&auth, "/delete", auth_delete_handler_v2);
    cHTTPX_RouteUse(auth_delete_account_route, authenticate_middleware);

    /* User routes */
    chttpx_router_t user = cHTTPX_RouteGroup(&api, "/users");
    cHTTPX_RouterUse(&user, authenticate_middleware);

    cHTTPX_Get(&user, "/me", user_me_handler_v2);
    cHTTPX_Patch(&user, "/me", user_update_profile_handler_v2);
    cHTTPX_Get(&user, "/{user_uid}", user_get_profile_handler_v2);

    chttpx_route_t* user_avatar_route = cHTTPX_Patch(&user, "/me/avatar", user_upload_avatar_handler_v2);
    cHTTPX_RouteUploadPolicy(user_avatar_route, &avatar_policy);

    /* Chat routes */
    chttpx_router_t chat = cHTTPX_RouteGroup(&api, "/chats");
    cHTTPX_RouterUse(&chat, authenticate_middleware);

    cHTTPX_Get(&chat, "", chat_get_my_history_handler_v2);
    cHTTPX_Get(&chat, "/u/{username}", chat_get_by_username_handler_v2);
    cHTTPX_Get(&chat, "/{chat_id}", chat_get_settings_handler_v2);

    chttpx_route_t* chat_background_route = cHTTPX_Patch(&chat, "/{chat_id}/cbackground", chat_upload_custom_bg_handler_v2);
    cHTTPX_RouteUploadPolicy(chat_background_route, &background_policy);

    cHTTPX_Post(&chat, "/{chat_id}/translate", chat_translate_handler_v2);
    cHTTPX_Post(&chat, "/bot/ai", chat_bot_default_ai_handler_v2);

    /* Media routes */
    chttpx_router_t media = cHTTPX_RouteGroup(&api, "/media");
    cHTTPX_RouterUse(&media, authenticate_middleware);

    chttpx_route_t* media_upload_route = cHTTPX_Post(&media, "/chats/{chat_id}/upload", media_upload_in_chat_handler_v2);
    cHTTPX_RouteUploadPolicy(media_upload_route, &media_policy);

    /* Group chats routes */
    chttpx_router_t group = cHTTPX_RouteGroup(&api, "/groups");
    cHTTPX_RouterUse(&group, authenticate_middleware);

    cHTTPX_Post(&group, "", group_chats_create_handler_v2);
    cHTTPX_Put(&group, "/{group_id}", group_chats_edit_handler_v2);
    cHTTPX_Delete(&group, "/{group_id}", group_chats_delete_handler_v2);
}

void moderation_routes(chttpx_serv_t* server)
{
	chttpx_router_t api = cHTTPX_RoutePathPrefix(server, "/api/v2");

	/* Moderation routes */
    chttpx_router_t moderation = cHTTPX_RouteGroup(&api, "/moderation");
    cHTTPX_RouterUse(&moderation, authenticate_middleware);
    cHTTPX_Post(&moderation, "/scan", moderation_wtype_handler_v2);
}
