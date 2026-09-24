#include "routes.h"

#include "handlers.h"
#include "middlewarex.h"

void http_routes(chttpx_serv_t* server)
{
    chttpx_router_t api = cHTTPX_RoutePathPrefix(server, "/api/v2");

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
}
