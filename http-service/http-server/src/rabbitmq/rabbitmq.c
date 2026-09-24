#define _POSIX_C_SOURCE 200809L

#include "rabbitmq.h"

#include <time.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <pthread.h>
#include <sys/time.h>
#include <sys/socket.h>

#include <amqp.h>
#include <amqp_framing.h>
#include <amqp_tcp_socket.h>
#include <amqp_ssl_socket.h>

#define CHANNEL 1

struct rmq_client
{
    amqp_connection_state_t connection;
    pthread_t owner;
    bool usable;
    bool publishing;
    bool consuming;
    bool in_callback;
    uint64_t next_sequence;
};

const char* rmq_result_locale_key(rmq_result_t result)
{
    static const char* const keys[] = {"rabbitmq.ok",
                                       "rabbitmq.idle",
                                       "rabbitmq.invalid-argument",
                                       "rabbitmq.not-configured",
                                       "rabbitmq.out-of-memory",
                                       "rabbitmq.connection-error",
                                       "rabbitmq.connection-lost",
                                       "rabbitmq.tls-error",
                                       "rabbitmq.timeout",
                                       "rabbitmq.auth-error",
                                       "rabbitmq.access-denied",
                                       "rabbitmq.not-found",
                                       "rabbitmq.topology-conflict",
                                       "rabbitmq.channel-closed",
                                       "rabbitmq.consumer-cancelled",
                                       "rabbitmq.unroutable",
                                       "rabbitmq.publish-rejected",
                                       "rabbitmq.publish-unknown",
                                       "rabbitmq.protocol-error",
                                       "rabbitmq.internal-error"};
    if ((unsigned)result >= sizeof(keys) / sizeof(keys[0]))
        return "rabbitmq.internal-error";
    return keys[result];
}

static rmq_result_t report(rmq_error_t* error, rmq_result_t code, const char* operation, int library_code, int system_errno, uint16_t broker_code, const char* detail)
{
    if (error)
    {
        memset(error, 0, sizeof(*error));
        error->code = code;
        error->library_code = library_code;
        error->system_errno = system_errno;
        error->broker_code = broker_code;
        snprintf(error->operation, sizeof(error->operation), "%s", operation);
        snprintf(error->detail, sizeof(error->detail), "%s", detail ? detail : rmq_result_locale_key(code));
    }
    return code;
}

static rmq_result_t check(rmq_client_t* client, const char* op, rmq_error_t* error)
{
    if (!client || !pthread_equal(client->owner, pthread_self()) || client->in_callback)
        return report(error, RMQ_INVALID_ARGUMENT, op, 0, 0, 0, "client must be used by its owner thread, outside callbacks");
    if (!client->usable)
        return report(error, RMQ_CONNECTION_LOST, op, 0, 0, 0, "client is unusable; disconnect and reconnect");
    return report(error, RMQ_OK, op, 0, 0, 0, NULL);
}

static rmq_result_t library_failure(rmq_client_t* client, const char* op, int status, int saved_errno, rmq_error_t* error)
{
    rmq_result_t code = RMQ_CONNECTION_LOST;
    switch (status)
    {
    case AMQP_STATUS_NO_MEMORY:
        code = RMQ_OUT_OF_MEMORY;
        break;
    case AMQP_STATUS_TIMEOUT:
    case AMQP_STATUS_HEARTBEAT_TIMEOUT:
        code = RMQ_TIMEOUT;
        break;
    case AMQP_STATUS_SSL_ERROR:
    case AMQP_STATUS_SSL_HOSTNAME_VERIFY_FAILED:
    case AMQP_STATUS_SSL_PEER_VERIFY_FAILED:
    case AMQP_STATUS_SSL_CONNECTION_FAILED:
        code = RMQ_TLS_ERROR;
        break;
    case AMQP_STATUS_BAD_AMQP_DATA:
    case AMQP_STATUS_UNKNOWN_CLASS:
    case AMQP_STATUS_UNKNOWN_METHOD:
    case AMQP_STATUS_UNEXPECTED_STATE:
        code = RMQ_PROTOCOL_ERROR;
        break;
    default:
        break;
    }
    if (strcmp(op, "connect") == 0 && code == RMQ_CONNECTION_LOST)
        code = RMQ_CONNECTION_ERROR;
    if (client)
        client->usable = false;
    /* errno is meaningful only for a socket/TCP failure. */
    if (status != AMQP_STATUS_SOCKET_ERROR && status != AMQP_STATUS_TCP_ERROR)
        saved_errno = 0;
    return report(error, code, op, status, saved_errno, 0, amqp_error_string2(status));
}

static rmq_result_t server_failure(rmq_client_t* client, const char* op, amqp_method_t method, rmq_error_t* error)
{
    uint16_t code = 0;
    amqp_bytes_t text = amqp_empty_bytes;
    rmq_result_t result = RMQ_PROTOCOL_ERROR;
    if (method.id == AMQP_CHANNEL_CLOSE_METHOD)
    {
        amqp_channel_close_t* close = method.decoded;
        code = close->reply_code;
        text = close->reply_text;
        result = RMQ_CHANNEL_CLOSED;
    }
    else if (method.id == AMQP_CONNECTION_CLOSE_METHOD)
    {
        amqp_connection_close_t* close = method.decoded;
        code = close->reply_code;
        text = close->reply_text;
        result = RMQ_CONNECTION_LOST;
    }
    else if (method.id == AMQP_BASIC_CANCEL_METHOD)
    {
        result = RMQ_CONSUMER_CANCELLED;
    }
    switch (code)
    {
    case 403:
        result = strcmp(op, "login") == 0 ? RMQ_AUTH_ERROR : RMQ_ACCESS_DENIED;
        break;
    case 530:
        result = RMQ_ACCESS_DENIED;
        break;
    case 404:
        result = RMQ_NOT_FOUND;
        break;
    case 405:
    case 406:
        result = RMQ_TOPOLOGY_CONFLICT;
        break;
    default:
        break;
    }
    char detail[512];
    snprintf(detail, sizeof(detail), "%s; AMQP=%u: %.*s", rmq_result_locale_key(result), code, (int)(text.len > 350 ? 350 : text.len),
             text.bytes ? (char*)text.bytes : "");
    client->usable = false;
    return report(error, result, op, 0, 0, code, detail);
}

static rmq_result_t rpc_result(rmq_client_t* client, const char* op, amqp_rpc_reply_t reply, rmq_error_t* error)
{
    int saved_errno = errno;
    if (reply.reply_type == AMQP_RESPONSE_NORMAL)
        return report(error, RMQ_OK, op, 0, 0, 0, NULL);
    if (reply.reply_type == AMQP_RESPONSE_SERVER_EXCEPTION)
        return server_failure(client, op, reply.reply, error);
    return library_failure(client, op, reply.reply_type == AMQP_RESPONSE_LIBRARY_EXCEPTION ? reply.library_error : AMQP_STATUS_BAD_AMQP_DATA,
                           saved_errno, error);
}

static bool short_string(const char* text)
{
    return text && strlen(text) <= 255;
}

static struct timeval milliseconds(int ms)
{
    return (struct timeval){.tv_sec = ms / 1000, .tv_usec = (ms % 1000) * 1000};
}

static int64_t now_ms(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static int wait_frame(rmq_client_t* client, amqp_frame_t* frame, int64_t deadline)
{
    int64_t remaining = deadline - now_ms();
    struct timeval timeout = milliseconds(remaining <= 0 ? 0 : remaining > INT_MAX ? INT_MAX : (int)remaining);
    return amqp_simple_wait_frame_noblock(client->connection, frame, &timeout);
}

rmq_result_t rmq_connect(rmq_client_t** out, const rmq_config_t* config, rmq_error_t* error)
{
    if (out)
        *out = NULL;
    if (!out || !config || config->connect_timeout_ms < 0 || config->rpc_timeout_ms < 0 || config->heartbeat_seconds < 0 ||
        config->heartbeat_seconds > 65535)
        return report(error, RMQ_INVALID_ARGUMENT, "connect", 0, 0, 0, NULL);
    if (!config->url || !*config->url)
        return report(error, RMQ_NOT_CONFIGURED, "connect", 0, 0, 0, NULL);

    char* url = strdup(config->url);
    if (!url)
        return report(error, RMQ_OUT_OF_MEMORY, "connect", 0, 0, 0, NULL);
    struct amqp_connection_info info;
    amqp_default_connection_info(&info);
    if (amqp_parse_url(url, &info) != AMQP_STATUS_OK)
    {
        free(url);
        return report(error, RMQ_INVALID_ARGUMENT, "connect", 0, 0, 0, "invalid AMQP URL");
    }
    rmq_client_t* client = calloc(1, sizeof(*client));
    if (!client)
    {
        free(url);
        return report(error, RMQ_OUT_OF_MEMORY, "connect", 0, 0, 0, NULL);
    }
    client->owner = pthread_self();
    client->connection = amqp_new_connection();
    client->next_sequence = 1;
    rmq_result_t result = RMQ_OUT_OF_MEMORY;
    if (!client->connection)
    {
        report(error, result, "connect", 0, 0, 0, NULL);
        goto fail;
    }
    amqp_socket_t* socket = info.ssl ? amqp_ssl_socket_new(client->connection) : amqp_tcp_socket_new(client->connection);
    if (!socket)
    {
        report(error, result, "connect", 0, 0, 0, NULL);
        goto fail;
    }
    if (info.ssl)
    {
        amqp_ssl_socket_set_verify_peer(socket, 1);
        amqp_ssl_socket_set_verify_hostname(socket, 1);
        if (config->tls_ca_file && amqp_ssl_socket_set_cacert(socket, config->tls_ca_file))
        {
            result = report(error, RMQ_TLS_ERROR, "connect", 0, 0, 0, "cannot load CA file");
            goto fail;
        }
    }
    struct timeval connect_timeout = milliseconds(config->connect_timeout_ms ? config->connect_timeout_ms : 3000);
    struct timeval rpc_timeout = milliseconds(config->rpc_timeout_ms ? config->rpc_timeout_ms : 5000);
    int status = amqp_set_handshake_timeout(client->connection, &connect_timeout);
    if (!status)
        status = amqp_set_rpc_timeout(client->connection, &rpc_timeout);
    if (!status)
    {
        errno = 0;
        status = amqp_socket_open_noblock(socket, info.host, info.port, &connect_timeout);
    }
    if (status)
    {
        result = library_failure(client, "connect", status, errno, error);
        goto fail;
    }
    /* Bound blocking writes; the frame-reading APIs enforce their own timeout. */
    if (setsockopt(amqp_socket_get_sockfd(socket), SOL_SOCKET, SO_SNDTIMEO, &rpc_timeout, sizeof(rpc_timeout)) != 0)
    {
        result = report(error, RMQ_INTERNAL_ERROR, "connect", 0, errno, 0, "cannot set socket write timeout");
        goto fail;
    }
    result = rpc_result(client, "login",
                        amqp_login(client->connection, info.vhost, 0, 131072, config->heartbeat_seconds ? config->heartbeat_seconds : 30,
                                   AMQP_SASL_METHOD_PLAIN, info.user, info.password),
                        error);
    if (result != RMQ_OK)
        goto fail;
    amqp_channel_open(client->connection, CHANNEL);
    result = rpc_result(client, "channel-open", amqp_get_rpc_reply(client->connection), error);
    if (result != RMQ_OK)
        goto fail;
    client->usable = true;
    free(url);
    *out = client;
    return report(error, RMQ_OK, "connect", 0, 0, 0, NULL);
fail:
    free(url);
    if (client->connection)
        amqp_destroy_connection(client->connection);
    free(client);
    return result;
}

void rmq_disconnect(rmq_client_t* client)
{
    if (!client || !pthread_equal(client->owner, pthread_self()) || client->in_callback)
        return;
    if (client->usable)
        amqp_connection_close(client->connection, AMQP_REPLY_SUCCESS);
    amqp_destroy_connection(client->connection);
    free(client);
}

rmq_result_t rmq_exchange_declare(rmq_client_t* client, const char* name, const char* type, bool durable, rmq_error_t* error)
{
    rmq_result_t result = check(client, "exchange-declare", error);
    if (result != RMQ_OK)
        return result;
    if (!short_string(name) || !*name || !short_string(type) || !*type)
        return report(error, RMQ_INVALID_ARGUMENT, "exchange-declare", 0, 0, 0, NULL);
    amqp_exchange_declare(client->connection, CHANNEL, amqp_cstring_bytes(name), amqp_cstring_bytes(type), 0, durable, 0, 0, amqp_empty_table);
    return rpc_result(client, "exchange-declare", amqp_get_rpc_reply(client->connection), error);
}

rmq_result_t rmq_queue_declare(rmq_client_t* client, const rmq_queue_config_t* config, rmq_error_t* error)
{
    rmq_result_t result = check(client, "queue-declare", error);
    if (result != RMQ_OK)
        return result;
    if (!config || !short_string(config->name) || !*config->name || config->message_ttl_ms < 0 ||
        (config->dead_letter_exchange && !short_string(config->dead_letter_exchange)) ||
        (config->dead_letter_routing_key && (!config->dead_letter_exchange || !short_string(config->dead_letter_routing_key))))
        return report(error, RMQ_INVALID_ARGUMENT, "queue-declare", 0, 0, 0, NULL);
    amqp_table_entry_t entries[3] = {0};
    amqp_table_t args = {.num_entries = 0, .entries = entries};
    if (config->dead_letter_exchange)
    {
        entries[args.num_entries++] =
            (amqp_table_entry_t){.key = amqp_cstring_bytes("x-dead-letter-exchange"),
                                 .value = {.kind = AMQP_FIELD_KIND_UTF8, .value.bytes = amqp_cstring_bytes(config->dead_letter_exchange)}};
    }
    if (config->dead_letter_routing_key)
    {
        entries[args.num_entries++] =
            (amqp_table_entry_t){.key = amqp_cstring_bytes("x-dead-letter-routing-key"),
                                 .value = {.kind = AMQP_FIELD_KIND_UTF8, .value.bytes = amqp_cstring_bytes(config->dead_letter_routing_key)}};
    }
    if (config->message_ttl_ms)
    {
        entries[args.num_entries++] = (amqp_table_entry_t){.key = amqp_cstring_bytes("x-message-ttl"),
                                                           .value = {.kind = AMQP_FIELD_KIND_I32, .value.i32 = config->message_ttl_ms}};
    }
    amqp_queue_declare(client->connection, CHANNEL, amqp_cstring_bytes(config->name), 0, config->durable, 0, config->auto_delete, args);
    return rpc_result(client, "queue-declare", amqp_get_rpc_reply(client->connection), error);
}

rmq_result_t rmq_queue_bind(rmq_client_t* client, const char* queue, const char* exchange, const char* routing_key, rmq_error_t* error)
{
    rmq_result_t result = check(client, "queue-bind", error);
    if (result != RMQ_OK)
        return result;
    if (!short_string(queue) || !*queue || !short_string(exchange) || !*exchange || !short_string(routing_key))
        return report(error, RMQ_INVALID_ARGUMENT, "queue-bind", 0, 0, 0, NULL);
    amqp_queue_bind(client->connection, CHANNEL, amqp_cstring_bytes(queue), amqp_cstring_bytes(exchange), amqp_cstring_bytes(routing_key),
                    amqp_empty_table);
    return rpc_result(client, "queue-bind", amqp_get_rpc_reply(client->connection), error);
}

static rmq_result_t unknown_publish(rmq_client_t* client, rmq_error_t* error)
{
    client->usable = false;
    if (error)
    {
        error->code = RMQ_PUBLISH_UNKNOWN;
        snprintf(error->operation, sizeof(error->operation), "publish");
        /* Keep the underlying failure detail and codes. */
    }
    return RMQ_PUBLISH_UNKNOWN;
}

rmq_result_t rmq_publish(rmq_client_t* client, const rmq_publish_t* message, int confirm_timeout_ms, rmq_error_t* error)
{
    rmq_result_t result = check(client, "publish", error);
    if (result != RMQ_OK)
        return result;
    if (!message || client->consuming || confirm_timeout_ms <= 0 || !short_string(message->exchange) || !short_string(message->routing_key) ||
        (!message->body.data && message->body.size) || (message->content_type && !short_string(message->content_type)) ||
        (message->message_id && !short_string(message->message_id)) || (message->correlation_id && !short_string(message->correlation_id)) ||
        (message->reply_to && !short_string(message->reply_to)))
        return report(error, RMQ_INVALID_ARGUMENT, "publish", 0, 0, 0, NULL);
    if (!client->publishing)
    {
        amqp_confirm_select(client->connection, CHANNEL);
        result = rpc_result(client, "confirm-select", amqp_get_rpc_reply(client->connection), error);
        if (result != RMQ_OK)
            return result;
        client->publishing = true;
    }
    amqp_basic_properties_t props = {0};
    props._flags = AMQP_BASIC_DELIVERY_MODE_FLAG;
    props.delivery_mode = message->persistent ? 2 : 1;
#define PROPERTY(field, flag)                                                                                                                        \
    do                                                                                                                                               \
    {                                                                                                                                                \
        if (message->field)                                                                                                                          \
        {                                                                                                                                            \
            props._flags |= flag;                                                                                                                    \
            props.field = amqp_cstring_bytes(message->field);                                                                                        \
        }                                                                                                                                            \
    } while (0)
    PROPERTY(content_type, AMQP_BASIC_CONTENT_TYPE_FLAG);
    PROPERTY(message_id, AMQP_BASIC_MESSAGE_ID_FLAG);
    PROPERTY(correlation_id, AMQP_BASIC_CORRELATION_ID_FLAG);
    PROPERTY(reply_to, AMQP_BASIC_REPLY_TO_FLAG);
#undef PROPERTY
    uint64_t sequence = client->next_sequence++;
    int64_t deadline = now_ms() + confirm_timeout_ms;
    errno = 0;
    int status = amqp_basic_publish(client->connection, CHANNEL, amqp_cstring_bytes(message->exchange), amqp_cstring_bytes(message->routing_key),
                                    message->mandatory, 0, &props, (amqp_bytes_t){.len = message->body.size, .bytes = (void*)message->body.data});
    if (status)
    {
        library_failure(client, "publish", status, errno, error);
        return unknown_publish(client, error);
    }
    bool returned = false, return_header = false;
    uint64_t return_remaining = 0;
    uint16_t return_code = 0;
    char return_detail[512] = {0};
    for (;;)
    {
        amqp_frame_t frame;
        status = wait_frame(client, &frame, deadline);
        if (status)
        {
            library_failure(client, "publish-confirm", status, errno, error);
            return unknown_publish(client, error);
        }
        if (frame.frame_type == AMQP_FRAME_HEARTBEAT)
            continue;
        if (frame.frame_type == AMQP_FRAME_METHOD &&
            (frame.payload.method.id == AMQP_CHANNEL_CLOSE_METHOD || frame.payload.method.id == AMQP_CONNECTION_CLOSE_METHOD))
        {
            result = server_failure(client, "publish", frame.payload.method, error);
            /* A broker shutdown may race with an accepted publication.
             * Only explicit channel-level rejection proves non-acceptance. */
            if (frame.payload.method.id == AMQP_CHANNEL_CLOSE_METHOD &&
                (result == RMQ_NOT_FOUND || result == RMQ_ACCESS_DENIED || result == RMQ_TOPOLOGY_CONFLICT))
                return result;
            return unknown_publish(client, error);
        }
        if (frame.channel != CHANNEL)
            goto protocol_error;
        if (return_header)
        {
            if (frame.frame_type != AMQP_FRAME_HEADER || frame.payload.properties.class_id != AMQP_BASIC_CLASS)
                goto protocol_error;
            return_remaining = frame.payload.properties.body_size;
            return_header = false;
            continue;
        }
        if (return_remaining)
        {
            if (frame.frame_type != AMQP_FRAME_BODY || !frame.payload.body_fragment.len || frame.payload.body_fragment.len > return_remaining)
                goto protocol_error;
            return_remaining -= frame.payload.body_fragment.len;
            continue;
        }
        if (frame.frame_type != AMQP_FRAME_METHOD)
            goto protocol_error;
        if (frame.payload.method.id == AMQP_BASIC_RETURN_METHOD)
        {
            if (returned)
                goto protocol_error;
            amqp_basic_return_t* ret = frame.payload.method.decoded;
            return_code = ret->reply_code;
            snprintf(return_detail, sizeof(return_detail), "AMQP=%u: %.*s", return_code, (int)(ret->reply_text.len > 400 ? 400 : ret->reply_text.len),
                     (char*)ret->reply_text.bytes);
            returned = return_header = true;
            continue;
        }
        if (frame.payload.method.id == AMQP_BASIC_ACK_METHOD || frame.payload.method.id == AMQP_BASIC_NACK_METHOD)
        {
            bool nack = frame.payload.method.id == AMQP_BASIC_NACK_METHOD;
            uint64_t tag;
            bool multiple;
            if (nack)
            {
                amqp_basic_nack_t* ack = frame.payload.method.decoded;
                tag = ack->delivery_tag;
                multiple = ack->multiple;
            }
            else
            {
                amqp_basic_ack_t* ack = frame.payload.method.decoded;
                tag = ack->delivery_tag;
                multiple = ack->multiple;
            }
            if (tag != sequence && !(multiple && tag == 0))
                goto protocol_error;
            result = returned ? RMQ_UNROUTABLE : nack ? RMQ_PUBLISH_REJECTED : RMQ_OK;
            amqp_maybe_release_buffers(client->connection);
            return report(error, result, "publish", 0, 0, return_code, returned ? return_detail : NULL);
        }
    protocol_error:
        report(error, RMQ_PROTOCOL_ERROR, "publish-confirm", 0, 0, 0, "unexpected confirmation frame");
        return unknown_publish(client, error);
    }
}

rmq_result_t rmq_subscribe(rmq_client_t* client, const char* queue, uint16_t prefetch, rmq_error_t* error)
{
    rmq_result_t result = check(client, "subscribe", error);
    if (result != RMQ_OK)
        return result;
    if (!short_string(queue) || !*queue || !prefetch || client->publishing || client->consuming)
        return report(error, RMQ_INVALID_ARGUMENT, "subscribe", 0, 0, 0, NULL);
    amqp_basic_qos(client->connection, CHANNEL, 0, prefetch, 0);
    result = rpc_result(client, "qos", amqp_get_rpc_reply(client->connection), error);
    if (result != RMQ_OK)
        return result;
    amqp_basic_consume(client->connection, CHANNEL, amqp_cstring_bytes(queue), amqp_empty_bytes, 0, 0, 0, amqp_empty_table);
    result = rpc_result(client, "subscribe", amqp_get_rpc_reply(client->connection), error);
    if (result == RMQ_OK)
        client->consuming = true;
    return result;
}

rmq_result_t rmq_poll(rmq_client_t* client, int wait_timeout_ms, rmq_error_t* error)
{
    rmq_result_t result = check(client, "poll", error);
    if (result != RMQ_OK)
        return result;
    if (client->consuming || wait_timeout_ms < 0)
        return report(error, RMQ_INVALID_ARGUMENT, "poll", 0, 0, 0, NULL);
    int64_t deadline = now_ms() + wait_timeout_ms;
    for (;;)
    {
        amqp_frame_t frame;
        int status = wait_frame(client, &frame, deadline);
        if (status == AMQP_STATUS_TIMEOUT)
            return report(error, RMQ_IDLE, "poll", 0, 0, 0, NULL);
        if (status)
            return library_failure(client, "poll", status, errno, error);
        if (frame.frame_type == AMQP_FRAME_HEARTBEAT)
            continue;
        if (frame.frame_type == AMQP_FRAME_METHOD)
            return server_failure(client, "poll", frame.payload.method, error);
        return library_failure(client, "poll", AMQP_STATUS_BAD_AMQP_DATA, 0, error);
    }
}

static rmq_bytes_t bytes(amqp_bytes_t value)
{
    return (rmq_bytes_t){.data = value.bytes, .size = value.len};
}

rmq_result_t rmq_consume_one(rmq_client_t* client, int wait_timeout_ms, rmq_handler_t handler, void* userdata, rmq_error_t* error)
{
    rmq_result_t result = check(client, "consume", error);
    if (result != RMQ_OK)
        return result;
    if (!client->consuming || !handler || wait_timeout_ms < 0)
        return report(error, RMQ_INVALID_ARGUMENT, "consume", 0, 0, 0, NULL);
    amqp_maybe_release_buffers(client->connection);
    struct timeval timeout = milliseconds(wait_timeout_ms);
    amqp_envelope_t envelope;
    amqp_rpc_reply_t reply = amqp_consume_message(client->connection, &envelope, &timeout, 0);
    if (reply.reply_type == AMQP_RESPONSE_LIBRARY_EXCEPTION && reply.library_error == AMQP_STATUS_TIMEOUT)
        return report(error, RMQ_IDLE, "consume", 0, 0, 0, NULL);
    if (reply.reply_type == AMQP_RESPONSE_LIBRARY_EXCEPTION && reply.library_error == AMQP_STATUS_UNEXPECTED_STATE)
    {
        amqp_frame_t frame;
        int status = wait_frame(client, &frame, now_ms());
        if (status)
            return library_failure(client, "consume", status, errno, error);
        if (frame.frame_type == AMQP_FRAME_METHOD)
            return server_failure(client, "consume", frame.payload.method, error);
    }
    result = rpc_result(client, "consume", reply, error);
    if (result != RMQ_OK)
        return result;
    amqp_basic_properties_t* props = &envelope.message.properties;
    rmq_message_t message = {.body = bytes(envelope.message.body),
                             .exchange = bytes(envelope.exchange),
                             .routing_key = bytes(envelope.routing_key),
                             .redelivered = envelope.redelivered};
#define COPY_PROPERTY(field, flag)                                                                                                                   \
    do                                                                                                                                               \
    {                                                                                                                                                \
        if (props->_flags & flag)                                                                                                                    \
            message.field = bytes(props->field);                                                                                                     \
    } while (0)
    COPY_PROPERTY(content_type, AMQP_BASIC_CONTENT_TYPE_FLAG);
    COPY_PROPERTY(message_id, AMQP_BASIC_MESSAGE_ID_FLAG);
    COPY_PROPERTY(correlation_id, AMQP_BASIC_CORRELATION_ID_FLAG);
    COPY_PROPERTY(reply_to, AMQP_BASIC_REPLY_TO_FLAG);
#undef COPY_PROPERTY
    client->in_callback = true;
    rmq_action_t action = handler(&message, userdata);
    client->in_callback = false;
    int status;
    if (action == RMQ_ACK)
        status = amqp_basic_ack(client->connection, envelope.channel, envelope.delivery_tag, 0);
    else if (action == RMQ_REJECT || action == RMQ_REQUEUE)
        status = amqp_basic_nack(client->connection, envelope.channel, envelope.delivery_tag, 0, action == RMQ_REQUEUE);
    else
    {
        amqp_destroy_envelope(&envelope);
        client->usable = false; /* Disconnect will requeue the unacknowledged delivery. */
        return report(error, RMQ_INVALID_ARGUMENT, "callback", 0, 0, 0, "invalid callback action; reconnect required");
    }
    int saved_errno = errno;
    amqp_destroy_envelope(&envelope);
    if (status)
        return library_failure(client, "ack", status, saved_errno, error);
    return report(error, RMQ_OK, "consume", 0, 0, 0, NULL);
}
