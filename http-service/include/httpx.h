#ifndef HTTPX_H
#define HTTPX_H

#include "rabbitmq.h"

#include <stdbool.h>
#include <pthread.h>
#include <stdatomic.h>

#include <libpq-fe.h>
#include <maxminddb.h>

#define HTTPX_SERVER_PORT 8080
#define HTTPX_RABBITMQ_WORKERS 1

typedef struct {
	pthread_t thread;
    bool started;

	const char *queue;
    const char *url;

	rmq_handler_t handler;
    void *handler_context;

	atomic_bool *stop;

	rmq_action_t last_action;
} httpx_rabbitmq_worker_t;

typedef struct {
    PGconn* conn;
    MMDB_s geoip;

	char *rabbitmq_url;
    atomic_bool rabbitmq_stop;

	httpx_rabbitmq_worker_t rabbitmq_workers[
        HTTPX_RABBITMQ_WORKERS
    ];
} httpx_server_t;

extern httpx_server_t *http_server;

void http_init(void);

#endif
