#ifndef ROUTES_H
#define ROUTES_H

#include "rabbitmq.h"

#include <libchttpx/libchttpx.h>

typedef struct
{
    const char* queue;
    rmq_handler_t handler;
} rabbitmq_route_t;

void http_routes(chttpx_serv_t* server);
void moderation_routes(chttpx_serv_t* server);

const rabbitmq_route_t* rabbitmq_routes(size_t* count);

#endif
