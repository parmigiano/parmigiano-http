#ifndef RABBITMQ_APP_H
#define RABBITMQ_APP_H

#include "rabbitmq.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    RABBITMQ_ROUTE_MODERATION = 0,
} rabbitmq_route_id_t;

typedef struct {
    rabbitmq_route_id_t id;

    const char* exchange;
    const char* exchange_type;

    const char* queue;
    const char* routing_key;

    uint16_t prefetch;
    bool durable;

    rmq_handler_t handler;
    void* handler_context;
} rabbitmq_route_t;

typedef struct rabbitmq_runtime rabbitmq_runtime_t;

const rabbitmq_route_t* rabbitmq_routes(size_t* count);
const rabbitmq_route_t* rabbitmq_route_get(rabbitmq_route_id_t id);

rmq_result_t rabbitmq_setup(
    rmq_client_t* client,
    rmq_error_t* error
);

bool rabbitmq_runtime_start(rabbitmq_runtime_t** out);
void rabbitmq_runtime_stop(rabbitmq_runtime_t* runtime);

rmq_result_t rabbitmq_publish_json(
    rabbitmq_route_id_t route_id,
    const void* data,
    size_t size,
    const char* message_id,
    rmq_error_t* error
);

#endif
