#include "rabbitmq_app.h"

#include "handlers.h"

static const rabbitmq_route_t routes[] = {
    {
        .id = RABBITMQ_ROUTE_MODERATION,
        .exchange = "moderation",
        .exchange_type = "direct",
        .queue = "moderation.data",
        .routing_key = "moderation.scan",
        .prefetch = 1,
        .durable = true,
        .handler = moderation_data_handler_v2,
        .handler_context = NULL,
    },
};

const rabbitmq_route_t* rabbitmq_routes(size_t* count)
{
    if (count)
        *count = sizeof(routes) / sizeof(routes[0]);

    return routes;
}

const rabbitmq_route_t* rabbitmq_route_get(rabbitmq_route_id_t id)
{
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i)
    {
        if (routes[i].id == id)
            return &routes[i];
    }

    return NULL;
}

rmq_result_t rabbitmq_setup(rmq_client_t* client, rmq_error_t* error)
{
    size_t count = 0;
    const rabbitmq_route_t* items = rabbitmq_routes(&count);

    for (size_t i = 0; i < count; ++i)
    {
        const rabbitmq_route_t* route = &items[i];

        rmq_result_t result = rmq_exchange_declare(client, route->exchange, route->exchange_type, route->durable, error);

        if (result != RMQ_OK)
            return result;

        if (route->queue && *route->queue)
        {
            rmq_queue_config_t queue = {
                .name = route->queue,
                .durable = route->durable,
                .auto_delete = false,
                .dead_letter_exchange = NULL,
                .dead_letter_routing_key = NULL,
                .message_ttl_ms = 0,
            };

            result = rmq_queue_declare(client, &queue, error);
            if (result != RMQ_OK)
                return result;

            result = rmq_queue_bind(client, route->queue, route->exchange, route->routing_key, error);

            if (result != RMQ_OK)
                return result;
        }
    }

    return RMQ_OK;
}
