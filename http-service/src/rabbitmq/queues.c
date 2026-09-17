#include "rabbitmq.h"

rmq_result_t rabbitmq_setup(rmq_client_t *client, rmq_error_t *error)
{
    rmq_result_t result = rmq_exchange_declare(client, MODERATION_EXCHANGE, "direct", true, error);
    if (result != RMQ_OK)
        return result;

    const struct {
        const char *queue;
        const char *routing_key;
    } routes[] = {
        {MODERATION_QUEUE, MODERATION_ROUTING_KEY},
    };

    const size_t count = sizeof(routes) / sizeof(routes[0]);
    for (size_t i = 0; i < count; ++i)
    {
        rmq_queue_config_t queue = {
            .name = routes[i].queue,
            .durable = true,
            .auto_delete = false,
            .dead_letter_exchange = NULL,
            .dead_letter_routing_key = NULL,
            .message_ttl_ms = 0,
        };

        result = rmq_queue_declare(client, &queue, error);
        if (result != RMQ_OK)
            return result;

        result = rmq_queue_bind(client, routes[i].queue, MODERATION_EXCHANGE, routes[i].routing_key, error);
        if (result != RMQ_OK)
            return result;
    }

    return RMQ_OK;
}
