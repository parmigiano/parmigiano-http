#include "routes.h"

#include "handlers.h"
#include "middlewarex.h"

void auth_routes(chttpx_serv_t* server)
{
    chttpx_router_t api = cHTTPX_RoutePathPrefix(server, "/api/v2");
    chttpx_router_t auth = cHTTPX_RouteGroup(&api, "/auth");

    cHTTPX_Post(&auth, "/login", auth_login_handler_v2);
    cHTTPX_Post(&auth, "/create", auth_create_handler_v2);
    cHTTPX_Post(&auth, "/confirm/email", auth_confirm_email_handler_v2);
    cHTTPX_Post(&auth, "/verify", auth_verify_handler_v2);

    chttpx_route_t* logout = cHTTPX_Post(&auth, "/logout", auth_logout_handler_v2);
    cHTTPX_RouteUse(logout, authenticate_middleware);

    chttpx_route_t* delete_account = cHTTPX_Delete(&auth, "/delete", auth_delete_handler_v2);
    cHTTPX_RouteUse(delete_account, authenticate_middleware);
}
