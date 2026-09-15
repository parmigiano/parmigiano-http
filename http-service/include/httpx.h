#ifndef HTTPX_H
#define HTTPX_H

#include <libpq-fe.h>
#include <maxminddb.h>

#define HTTPX_SERVER_PORT 8080

typedef struct {
    PGconn* conn;
    MMDB_s geoip;
} httpx_server_t;

extern httpx_server_t *http_server;

void http_init(void);

#endif