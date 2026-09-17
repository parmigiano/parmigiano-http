#ifndef RABBITMQ_H
#define RABBITMQ_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* QUEUEs */
#define MODERATION_QUEUE   "moderation.data"

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
    RMQ_TOPOLOGY_CONFLICT,   /* Очередь/exchange существуют с другими параметрами */
    RMQ_CHANNEL_CLOSED,
    RMQ_CONSUMER_CANCELLED,

    RMQ_UNROUTABLE,          /* Сообщение не попало ни в одну очередь */
    RMQ_PUBLISH_REJECTED,    /* Брокер прислал publisher nack */
    RMQ_PUBLISH_UNKNOWN,     /* Отправили, но подтверждение не получили */

    RMQ_PROTOCOL_ERROR,
    RMQ_INTERNAL_ERROR
} rmq_result_t;

typedef struct {
    rmq_result_t code;

	/* Исходный код rabbitmq-c */
    int library_code;
	/* Если удалось получить */
    int system_errno;
	/* Код ошибки AMQP */
    uint16_t broker_code;

	/* connect, publish, consume, ack... */
    char operation[64];
	/* Технические подробности для лога */
    char detail[512];
} rmq_error_t;

typedef struct {
	/* amqp://... либо amqps://... */
    const char *url;

    int connect_timeout_ms; /* 0 = 3000; OS DNS resolution may take longer */
    int rpc_timeout_ms;     /* 0 = 5000; also the socket write timeout */
    int heartbeat_seconds; /* 0 = 30 */

    /* Для amqps: проверка сертификата и hostname включена. */
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
	/* Обработка завершена */
    RMQ_ACK,
	/* Вернуть сообщение в очередь */
    RMQ_REQUEUE,
	/* Отправить в DLX, если настроен; иначе удалить */
    RMQ_REJECT
} rmq_action_t;

typedef rmq_action_t (*rmq_handler_t)(
    const rmq_message_t *message,
    void *userdata
);

rmq_result_t rabbitmq_setup(
    rmq_client_t *client,
    rmq_error_t *error
);

/* One client per owning thread. Do not mix publishing and consuming on it.
 * Callback data is borrowed until callback returns. Callbacks must be short:
 * for long jobs, hand work to workers and implement an asynchronous ACK layer.
 * After a connection/channel error disconnect and recreate the client.
 * No automatic reconnect/replay: uncertain publications may already exist.
 */
typedef struct {
    const char *name;
    bool durable;
    bool auto_delete;
    const char *dead_letter_exchange; /* NULL = unspecified; "" = default */
    const char *dead_letter_routing_key;
    int message_ttl_ms;              /* 0 = unspecified */
} rmq_queue_config_t;

rmq_result_t rmq_exchange_declare(rmq_client_t *client, const char *name, const char *type, bool durable, rmq_error_t *error);
rmq_result_t rmq_queue_declare(rmq_client_t *client, const rmq_queue_config_t *config, rmq_error_t *error);
rmq_result_t rmq_queue_bind(rmq_client_t *client, const char *queue, const char *exchange, const char *routing_key, rmq_error_t *error);

/* Service heartbeats on an idle publisher, from its owning thread. */
rmq_result_t rmq_poll(rmq_client_t *client, int wait_timeout_ms, rmq_error_t *error);

rmq_result_t rmq_connect(rmq_client_t **out, const rmq_config_t *config, rmq_error_t *error);

void rmq_disconnect(rmq_client_t *client);

/* Wait for broker confirmation (not processing by a consumer).
 * RMQ_PUBLISH_UNKNOWN requires reconnect and deduplicated retry.
 * confirm_timeout_ms must be > 0; writes use rpc_timeout_ms independently.
 */
rmq_result_t rmq_publish(rmq_client_t *client, const rmq_publish_t *message, int confirm_timeout_ms, rmq_error_t *error);

rmq_result_t rmq_subscribe(rmq_client_t *client, const char *queue, uint16_t prefetch, rmq_error_t *error);

/* wait_timeout_ms >= 0 bounds waiting for delivery, not callback/body transfer.
 * Callback memory is borrowed. ACK is sent only after the callback returns.
 * RMQ_OK does not exclude redelivery after a connection loss.
 */
rmq_result_t rmq_consume_one(rmq_client_t *client, int wait_timeout_ms, rmq_handler_t handler, void *userdata, rmq_error_t *error);

const char *rmq_result_locale_key(rmq_result_t result);

#endif
