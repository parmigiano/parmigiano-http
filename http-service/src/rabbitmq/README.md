# RabbitMQ layout

Application-level RabbitMQ configuration lives in this directory.

- `rabbitmq.c` — low-level AMQP transport: connect, publish, consume, ACK/NACK, confirms and errors.
- `channels.c` — the single registry of exchanges, queues, routing keys and consumer handlers.
- `runtime.c` — consumer worker lifecycle, reconnect loop and the high-level JSON publisher.

## Add a new channel

1. Add a new value to `rabbitmq_channel_id_t` in `include/rabbitmq_app.h`.
2. Add one entry to the `channels[]` array in `channels.c`.
3. Implement the consumer handler if the channel is consumed by this service.

No changes are required in `httpx.c`, `httpx.h`, HTTP routes or worker capacity constants.

Example:

```c
{
    .id = RABBITMQ_CHANNEL_NOTIFICATIONS,
    .exchange = "notifications",
    .exchange_type = "direct",
    .queue = "notifications.data",
    .routing_key = "notifications.send",
    .prefetch = 8,
    .durable = true,
    .handler = notifications_handler,
    .handler_context = NULL,
},
```

To publish JSON:

```c
rabbitmq_publish_json(
    RABBITMQ_CHANNEL_NOTIFICATIONS,
    json,
    strlen(json),
    request_id,
    &error
);
```
