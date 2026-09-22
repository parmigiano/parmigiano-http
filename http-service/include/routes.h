#ifndef ROUTES_H
#define ROUTES_H

#include <libchttpx/libchttpx.h>

void main_routes(chttpx_serv_t* server);
void auth_routes(chttpx_serv_t* server);
void moderation_routes(chttpx_serv_t* server);

#endif
