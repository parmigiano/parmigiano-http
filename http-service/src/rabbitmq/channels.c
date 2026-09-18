#include "rabbitmq_app.h"

#include "handlers.h"

static const rabbitmq_channel_t channels[] = {
    {
        .id = RABBITMQ_CHANNEL_MODERATION,
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

const rabbitmq_channel_t* rabbitmq_channels(size_t* count)
{
    if (count)
        *count = sizeof(channels) / sizeof(channels[0]);

    return channels;
}

const rabbitmq_channel_t* rabbitmq_channel_get(rabbitmq_channel_id_t id)
{
    for (size_t i = 0; i < sizeof(channels) / sizeof(channels[0]); ++i)
    {
        if (channels[i].id == id)
            return &channels[i];
    }

    return NULL;
}

rmq_result_t rabbitmq_setup(rmq_client_t* client, rmq_error_t* error)
{
    size_t count = 0;
    const rabbitmq_channel_t* items = rabbitmq_channels(&count);

    for (size_t i = 0; i < count; ++i)
    {
        const rabbitmq_channel_t* channel = &items[i];

        rmq_result_t result = rmq_exchange_declare(
            client,
            channel->exchange,
            channel->exchange_type,
            channel->durable,
            error
        );

        if (result != RMQ_OK)
            return result;

        rmq_queue_config_t queue = {
            .name = channel->queue,
            .durable = channel->durable,
            .auto_delete = false,
            .dead_letter_exchange = NULL,
            .dead_letter_routing_key = NULL,
            .message_ttl_ms = 0,
        };

        result = rmq_queue_declare(client, &queue, error);
        if (result != RMQ_OK)
            return result;

        result = rmq_queue_bind(
            client,
            channel->queue,
            channel->exchange,
            channel->routing_key,
            error
        );

        if (result != RMQ_OK)
            return result;
    }

    return RMQ_OK;
}
