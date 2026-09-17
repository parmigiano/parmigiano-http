#ifndef ROUTES_H
#define ROUTES_H

#include "rabbitmq.h"

typedef struct
{
    const char* queue;
    rmq_handler_t handler;
} rabbitmq_route_t;

void routes(void);

const rabbitmq_route_t* rabbitmq_routes(size_t* count);

#endif
