#ifndef HTTPX_H
#define HTTPX_H

#include <stdbool.h>
#include <libpq-fe.h>
#include <maxminddb.h>
#include <libchttpx/libchttpx.h>

#define HTTPX_SERVER_PORT 8081

typedef struct {
	chttpx_app_t app;
	bool app_initialized;

	chttpx_serv_t* http;

    PGconn* conn;
    MMDB_s geoip;
} app_context_t;

extern app_context_t *app_context;

void http_init(void);

#endif
