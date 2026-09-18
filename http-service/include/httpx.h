#ifndef HTTPX_H
#define HTTPX_H

#include <stdbool.h>
#include <libpq-fe.h>
#include <maxminddb.h>
#include <libchttpx/libchttpx.h>

#define HTTPX_SERVER_PORT 8080
#define MODERATION_SERVER_PORT 8181
typedef struct rabbitmq_runtime rabbitmq_runtime_t;

typedef struct {
	chttpx_app_t app;
	bool app_initialized;

	chttpx_serv_t* http;
	chttpx_serv_t* moderation;

    PGconn* conn;
    MMDB_s geoip;

    rabbitmq_runtime_t* rabbitmq;
} httpx_server_t;

extern httpx_server_t *http_server;

void http_init(void);

#endif
