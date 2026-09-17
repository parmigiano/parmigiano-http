#ifndef RABBITMQ_H
#define RABBITMQ_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Moderation topology */
#define MODERATION_EXCHANGE "moderation"
#define MODERATION_QUEUE "moderation.data"
#define MODERATION_ROUTING_KEY "moderation.scan"

typedef struct rmq_client rmq_client_t;

typedef enum {
    RMQ_OK = 0,
    RMQ_IDLE,

    RMQ_INVALID_ARGUMENT,
    RMQ_NOT_CONFIGURED,
    RMQ_OUT_OF_MEMORY,

    RMQ_CONNECTION_ERROR,
    RMQ_CONNECTION_LOST,
    RMQ_TLS_ERROR,
    RMQ_TIMEOUT,
    RMQ_AUTH_ERROR,
    RMQ_ACCESS_DENIED,

    RMQ_NOT_FOUND,
    RMQ_TOPOLOGY_CONFLICT,
    RMQ_CHANNEL_CLOSED,
    RMQ_CONSUMER_CANCELLED,

    RMQ_UNROUTABLE,
    RMQ_PUBLISH_REJECTED,
    RMQ_PUBLISH_UNKNOWN,

    RMQ_PROTOCOL_ERROR,
    RMQ_INTERNAL_ERROR
} rmq_result_t;

typedef struct {
    rmq_result_t code;
    int library_code;
    int system_errno;
    uint16_t broker_code;
    char operation[64];
    char detail[512];
} rmq_error_t;

typedef struct {
    const char *url;
    int connect_timeout_ms;
    int rpc_timeout_ms;
    int heartbeat_seconds;
    const char *tls_ca_file;
} rmq_config_t;

typedef struct {
    const void *data;
    size_t size;
} rmq_bytes_t;

typedef struct {
    const char *exchange;
    const char *routing_key;
    rmq_bytes_t body;
    const char *content_type;
    const char *message_id;
    const char *correlation_id;
    const char *reply_to;
    bool persistent;
    bool mandatory;
} rmq_publish_t;

typedef struct {
    rmq_bytes_t body;
    rmq_bytes_t exchange;
    rmq_bytes_t routing_key;
    rmq_bytes_t content_type;
    rmq_bytes_t message_id;
    rmq_bytes_t correlation_id;
    rmq_bytes_t reply_to;
    bool redelivered;
} rmq_message_t;

typedef enum {
    RMQ_ACK,
    RMQ_REQUEUE,
    RMQ_REJECT
} rmq_action_t;

typedef rmq_action_t (*rmq_handler_t)(const rmq_message_t *message, void *userdata);

rmq_result_t rabbitmq_setup(rmq_client_t *client, rmq_error_t *error);

typedef struct {
    const char *name;
    bool durable;
    bool auto_delete;
    const char *dead_letter_exchange;
    const char *dead_letter_routing_key;
    int message_ttl_ms;
} rmq_queue_config_t;

rmq_result_t rmq_exchange_declare(rmq_client_t *client, const char *name, const char *type, bool durable, rmq_error_t *error);
rmq_result_t rmq_queue_declare(rmq_client_t *client, const rmq_queue_config_t *config, rmq_error_t *error);
rmq_result_t rmq_queue_bind(rmq_client_t *client, const char *queue, const char *exchange, const char *routing_key, rmq_error_t *error);
rmq_result_t rmq_poll(rmq_client_t *client, int wait_timeout_ms, rmq_error_t *error);
rmq_result_t rmq_connect(rmq_client_t **out, const rmq_config_t *config, rmq_error_t *error);
void rmq_disconnect(rmq_client_t *client);
rmq_result_t rmq_publish(rmq_client_t *client, const rmq_publish_t *message, int confirm_timeout_ms, rmq_error_t *error);
rmq_result_t rmq_subscribe(rmq_client_t *client, const char *queue, uint16_t prefetch, rmq_error_t *error);
rmq_result_t rmq_consume_one(rmq_client_t *client, int wait_timeout_ms, rmq_handler_t handler, void *userdata, rmq_error_t *error);
const char *rmq_result_locale_key(rmq_result_t result);

#endif
