#include "httpx.h"

#include "logger.h"
#include "routes.h"
#include "utilities.h"
#include "middlewarex.h"
#include "redis/redis.h"
#include "postgres/postgres.h"

#include <maxminddb.h>
#include <curl/curl.h>
#include <libchttpx/libchttpx.h>

httpx_server_t* http_server = NULL;

static void _cors();

void http_init(void)
{
    /* Initial logger */
    logger_init();

    http_server = (httpx_server_t*)calloc(1, sizeof(httpx_server_t));
    if (!http_server)
    {
        logger_error("http_init: calloc failed for http_server");
        fprintf(stderr, "calloc failed\n");
        exit(1);
    }

    /* cHTTPX Server */
    chttpx_serv_t serv = {0};

    size_t max_clients = 8192;

    if (cHTTPX_Init(&serv, HTTPX_SERVER_PORT, &max_clients) != 0)
    {
        logger_error("http_init: failed to start server");
        fprintf(stderr, "failed to start server\n");
        exit(1);
    }

    /* Timeouts */
    serv.read_timeout_sec = 60;
    serv.write_timeout_sec = 60;
    serv.idle_timeout_sec = 90;

    /* Initial redis connect */
    if (!redis_conn())
    {
        logger_error("http_init: failed to start redis error");
        fprintf(stderr, "redis error\n");
        exit(1);
    }

    /* Inital database, migrations */
    PGconn* conn = db_conn();
    if (!conn)
    {
        logger_error("http_init: failed to connect to database");
        fprintf(stderr, "failed to connect to database\n");
        exit(1);
    }
    http_server->conn = conn;

    run_migrations(conn);

    /* Load in memory GeoIP */
    int status = MMDB_open("/usr/local/share/GeoIP/GeoLite2-Country.mmdb", MMDB_MODE_MMAP, &http_server->geoip);
    if (status != MMDB_SUCCESS)
    {
        logger_error("http_init: failed load GeoIP in memory: %s", MMDB_strerror(status));
        fprintf(stderr, "failed load GeoIP in memory: %s\n", MMDB_strerror(status));
        exit(1);
    }

    /* Initial AI queue */
    start_ai_worker();

    /* Cors */
    _cors();
    fprintf(stderr, "startup: CORS configured\n");

    /* Initial middlewares */
    cHTTPX_MiddlewareRecovery();
    cHTTPX_MiddlewareLogging();
    cHTTPX_MiddlewareRateLimiter(5, 1);
    cHTTPX_MiddlewareUse(language_middleware);
    cHTTPX_MiddlewareUse(x_request_id_middleware);
    cHTTPX_MiddlewareUse(geoip_block_middleware);
    cHTTPX_MiddlewareUse(body_entry_middleware);
    cHTTPX_MiddlewareUse(pow_ddos_middleware);
    cHTTPX_MiddlewareUse(authenticate_middleware);
    // cHTTPX_MiddlewareUse(email_confirmed_middleware);

    /* Initial routes */
    routes();
    fprintf(stderr, "startup: routes registered\n");

    /* At the very end, to start listening to incoming requests from users. */
    cHTTPX_Listen();

    /* Shutdown server */
    cHTTPX_Shutdown();

    /* Free mmdb GeoIP */
    MMDB_close(&http_server->geoip);

    if (http_server->conn)
    {
        db_close(http_server->conn);
        http_server->conn = NULL;
    }

    redis_disconnect();

    /* Free CURL */
    curl_global_cleanup();

    free(http_server);
    http_server = NULL;
}

static void _cors()
{
    const char* env_type = getenv("TYPE");
    if (!env_type)
        return;

    const char* allowed_origins_prod[1] = {
        "https://parmigianochat.ru",
    };

    const char* allowed_origins_dev[2] = {
        "http://localhost:8080",
        "http://localhost:80",
    };

    const char** allowed_origins;
    size_t origins_count;

    if (strcmp(env_type, "PROD") == 0)
    {
        allowed_origins = allowed_origins_prod;
        origins_count = sizeof(allowed_origins_prod) / sizeof(allowed_origins_prod[0]);
    }
    else
    {
        allowed_origins = allowed_origins_dev;
        origins_count = sizeof(allowed_origins_dev) / sizeof(allowed_origins_dev[0]);
    }

    cHTTPX_Cors(allowed_origins, origins_count, NULL,
                "Content-Type, Authorization, Accept-Language, X-Debug, Pow-Challenge, Pow-Nonce, X-Real-IP, X-Forwarded-For, X-Forwarded-Proto, "
                "Upgrade, Connection, Host");
}