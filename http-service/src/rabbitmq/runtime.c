#define _POSIX_C_SOURCE 200809L

#include "rabbitmq_app.h"

#include "logger.h"

#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    pthread_t thread;
    bool started;

    const rabbitmq_channel_t* channel;
    const char* url;

    atomic_bool* stop;
    rmq_action_t last_action;
} rabbitmq_worker_t;

struct rabbitmq_runtime {
    char* url;
    atomic_bool stop;

    rabbitmq_worker_t* workers;
    size_t worker_count;
};

static void rabbitmq_retry_pause(atomic_bool* stop)
{
    for (int i = 0; i < 50; ++i)
    {
        if (atomic_load(stop))
            return;

        struct timespec remaining = {
            .tv_sec = 0,
            .tv_nsec = 100000000L,
        };

        while (nanosleep(&remaining, &remaining) == -1)
        {
            if (errno != EINTR || atomic_load(stop))
                return;
        }
    }
}

static rmq_action_t rabbitmq_dispatch(const rmq_message_t* message, void* userdata)
{
    rabbitmq_worker_t* worker = userdata;

    if (atomic_load(worker->stop))
    {
        worker->last_action = RMQ_REQUEUE;
        return RMQ_REQUEUE;
    }

    worker->last_action = worker->channel->handler(
        message,
        worker->channel->handler_context
    );

    return worker->last_action;
}

static void* rabbitmq_worker_main(void* arg)
{
    rabbitmq_worker_t* worker = arg;

    rmq_config_t config = {
        .url = worker->url,
        .connect_timeout_ms = 3000,
        .rpc_timeout_ms = 5000,
        .heartbeat_seconds = 180,
        .tls_ca_file = NULL,
    };

    while (!atomic_load(worker->stop))
    {
        rmq_client_t* client = NULL;
        rmq_error_t error = {0};

        rmq_result_t result = rmq_connect(&client, &config, &error);
        if (result != RMQ_OK)
        {
            logger_error(
                "RabbitMQ queue=%s operation=%s error=%s",
                worker->channel->queue,
                error.operation,
                error.detail
            );

            rabbitmq_retry_pause(worker->stop);
            continue;
        }

        result = rabbitmq_setup(client, &error);

        if (result == RMQ_OK && !atomic_load(worker->stop))
        {
            result = rmq_subscribe(
                client,
                worker->channel->queue,
                worker->channel->prefetch,
                &error
            );
        }

        if (result != RMQ_OK)
        {
            logger_error(
                "RabbitMQ queue=%s operation=%s error=%s",
                worker->channel->queue,
                error.operation,
                error.detail
            );

            rmq_disconnect(client);
            rabbitmq_retry_pause(worker->stop);
            continue;
        }

        while (!atomic_load(worker->stop))
        {
            worker->last_action = RMQ_ACK;

            result = rmq_consume_one(
                client,
                1000,
                rabbitmq_dispatch,
                worker,
                &error
            );

            if (result == RMQ_IDLE)
                continue;

            if (result != RMQ_OK)
            {
                logger_error(
                    "RabbitMQ queue=%s operation=%s error=%s",
                    worker->channel->queue,
                    error.operation,
                    error.detail
                );
                break;
            }

            if (worker->last_action == RMQ_REQUEUE)
                break;
        }

        rmq_disconnect(client);

        if (!atomic_load(worker->stop))
            rabbitmq_retry_pause(worker->stop);
    }

    return NULL;
}

bool rabbitmq_runtime_start(rabbitmq_runtime_t** out)
{
    if (out)
        *out = NULL;

    if (!out)
        return false;

    const char* url = getenv("RABBITMQ_URL");
    if (!url || !*url)
    {
        logger_error("RabbitMQ: RABBITMQ_URL is empty");
        return false;
    }

    size_t channel_count = 0;
    const rabbitmq_channel_t* channels = rabbitmq_channels(&channel_count);

    if (!channels || channel_count == 0)
    {
        logger_warn("RabbitMQ: no channels configured");
        return true;
    }

    rabbitmq_runtime_t* runtime = calloc(1, sizeof(*runtime));
    if (!runtime)
        return false;

    runtime->url = strdup(url);
    runtime->workers = calloc(channel_count, sizeof(*runtime->workers));
    runtime->worker_count = channel_count;

    if (!runtime->url || !runtime->workers)
    {
        rabbitmq_runtime_stop(runtime);
        return false;
    }

    atomic_init(&runtime->stop, false);

    for (size_t i = 0; i < channel_count; ++i)
    {
        rabbitmq_worker_t* worker = &runtime->workers[i];

        worker->channel = &channels[i];
        worker->url = runtime->url;
        worker->stop = &runtime->stop;
        worker->last_action = RMQ_ACK;

        int rc = pthread_create(
            &worker->thread,
            NULL,
            rabbitmq_worker_main,
            worker
        );

        if (rc != 0)
        {
            logger_error(
                "RabbitMQ: cannot start worker queue=%s: %s",
                worker->channel->queue,
                strerror(rc)
            );

            rabbitmq_runtime_stop(runtime);
            return false;
        }

        worker->started = true;
    }

    *out = runtime;
    return true;
}

void rabbitmq_runtime_stop(rabbitmq_runtime_t* runtime)
{
    if (!runtime)
        return;

    atomic_store(&runtime->stop, true);

    if (runtime->workers)
    {
        for (size_t i = 0; i < runtime->worker_count; ++i)
        {
            rabbitmq_worker_t* worker = &runtime->workers[i];

            if (!worker->started)
                continue;

            pthread_join(worker->thread, NULL);
            worker->started = false;
        }
    }

    free(runtime->workers);
    free(runtime->url);
    free(runtime);
}

rmq_result_t rabbitmq_publish_json(
    rabbitmq_channel_id_t channel_id,
    const void* data,
    size_t size,
    const char* message_id,
    rmq_error_t* error
)
{
    if (!data || size == 0)
        return RMQ_INVALID_ARGUMENT;

    const rabbitmq_channel_t* channel = rabbitmq_channel_get(channel_id);
    if (!channel)
        return RMQ_NOT_FOUND;

    const char* url = getenv("RABBITMQ_URL");
    if (!url || !*url)
        return RMQ_NOT_CONFIGURED;

    rmq_config_t config = {
        .url = url,
        .connect_timeout_ms = 3000,
        .rpc_timeout_ms = 5000,
        .heartbeat_seconds = 120,
        .tls_ca_file = NULL,
    };

    rmq_client_t* client = NULL;
    rmq_result_t result = rmq_connect(&client, &config, error);
    if (result != RMQ_OK)
        return result;

    result = rabbitmq_setup(client, error);
    if (result == RMQ_OK)
    {
        rmq_publish_t publication = {
            .exchange = channel->exchange,
            .routing_key = channel->routing_key,
            .body = {
                .data = data,
                .size = size,
            },
            .content_type = "application/json",
            .message_id = message_id && *message_id ? message_id : NULL,
            .correlation_id = NULL,
            .reply_to = NULL,
            .persistent = true,
            .mandatory = true,
        };

        result = rmq_publish(
            client,
            &publication,
            5000,
            error
        );
    }

    rmq_disconnect(client);
    return result;
}
