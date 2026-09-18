# RabbitMQ layout

Application-level RabbitMQ configuration lives in this directory.

- `rabbitmq.c` — low-level AMQP transport: connect, publish, consume, ACK/NACK, confirms and errors.
- `routes.c` — the single registry of exchanges, queues, routing keys and consumer handlers.
- `runtime.c` — consumer worker lifecycle, reconnect loop and the high-level JSON publisher.

## Add a new route

1. Add a new value to `rabbitmq_route_id_t` in `include/rabbitmq_app.h`.
2. Add one entry to the `routes[]` array in `routes.c`.
3. Implement the consumer handler if the route is consumed by this service.

No changes are required in `httpx.c`, `httpx.h`, HTTP routes or worker capacity constants.

Example:

```c
{
    .id = RABBITMQ_ROUTE_NOTIFICATIONS,
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
    RABBITMQ_ROUTE_NOTIFICATIONS,
    json,
    strlen(json),
    request_id,
    &error
);
```
