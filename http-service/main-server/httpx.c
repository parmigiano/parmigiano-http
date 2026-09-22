#include "httpx.h"

#include "logger.h"
#include "routes.h"
#include "rabbitmq_app.h"
#include "utilities.h"
#include "middlewarex.h"
#include "redis/redis.h"
#include "postgres/postgres.h"

#include <string.h>
#include <maxminddb.h>
#include <curl/curl.h>
#include <libchttpx/libchttpx.h>

app_context_t* app_context = NULL;

static void _cors(chttpx_serv_t* server);
static void _http_cleanup(void);
static chttpx_config_t _server_config(uint16_t port, const char** languages, size_t languages_count);

void http_init(void)
{
    logger_init();

    app_context = calloc(1, sizeof(*app_context));
    if (!app_context)
    {
        logger_error("http_init: calloc failed for app_context");
        fprintf(stderr, "calloc failed\n");
        return;
    }

    chttpx_error_t app_result = cHTTPX_AppInit(&app_context->app);
    if (app_result != CHTTPX_OK)
    {
        logger_error("http_init: failed to initialize cHTTPX App, error=%d", app_result);
        fprintf(stderr, "failed to initialize libchttpx app, error=%d\n", app_result);
        _http_cleanup();
        return;
    }
    app_context->app_initialized = true;

    static const char* languages[] = {
        "en", "ru",
    };

    chttpx_config_t main_config = _server_config(MAIN_SERVER_PORT, languages, CHTTPX_ARRAY_LEN(languages));
    app_context->main = cHTTPX_AppServer(&app_context->app, "main", &main_config);
    if (!app_context->main)
    {
        logger_error("http_init: failed to create main server");
        fprintf(stderr, "failed to create main server\n");
        _http_cleanup();
        return;
    }
    logger_info("Main server created on port %d", MAIN_SERVER_PORT);

    chttpx_config_t auth_config = _server_config(AUTH_SERVER_PORT, languages, CHTTPX_ARRAY_LEN(languages));
    auth_config.max_body_size = 1024 * 1024;
    auth_config.max_upload_size = 1024 * 1024;

    app_context->auth = cHTTPX_AppServer(&app_context->app, "auth", &auth_config);
    if (!app_context->auth)
    {
        logger_error("http_init: failed to create auth server");
        fprintf(stderr, "failed to create auth server\n");
        _http_cleanup();
        return;
    }
    logger_info("Auth server created on internal port %d", AUTH_SERVER_PORT);

    chttpx_config_t moderation_config = _server_config(MODERATION_SERVER_PORT, languages, CHTTPX_ARRAY_LEN(languages));
    app_context->moderation = cHTTPX_AppServer(&app_context->app, "moderation", &moderation_config);
    if (!app_context->moderation)
    {
        logger_error("http_init: failed to create moderation server");
        fprintf(stderr, "failed to create moderation server\n");
        _http_cleanup();
        return;
    }
    logger_info("Moderation server created on port %d", MODERATION_SERVER_PORT);

    if (!redis_conn())
    {
        logger_error("http_init: failed to start redis error");
        fprintf(stderr, "redis error\n");
        _http_cleanup();
        return;
    }

    app_context->conn = db_conn();
    if (!app_context->conn)
    {
        logger_error("http_init: failed to connect to database");
        fprintf(stderr, "failed to connect to database\n");
        _http_cleanup();
        return;
    }

    run_migrations(app_context->conn);

    int status = MMDB_open("/usr/local/share/GeoIP/GeoLite2-Country.mmdb", MMDB_MODE_MMAP, &app_context->geoip);
    if (status != MMDB_SUCCESS)
    {
        logger_error("http_init: failed load GeoIP in memory: %s", MMDB_strerror(status));
        fprintf(stderr, "failed load GeoIP in memory: %s\n", MMDB_strerror(status));
        _http_cleanup();
        return;
    }

    start_ai_worker();

    _cors(app_context->main);
    _cors(app_context->moderation);

    cHTTPX_MiddlewareLogging(app_context->main);
    cHTTPX_MiddlewareLogging(app_context->auth);
    cHTTPX_MiddlewareLogging(app_context->moderation);

    cHTTPX_MiddlewareRateLimiter(app_context->main, 5, 1);
    cHTTPX_MiddlewareUse(app_context->main, geoip_block_middleware);

    main_routes(app_context->main);
    auth_routes(app_context->auth);
    moderation_routes(app_context->moderation);

    if (!rabbitmq_runtime_start(&app_context->rabbitmq))
        logger_error("Failed to start RabbitMQ runtime");

    int run_result = cHTTPX_AppRun(&app_context->app);
    if (run_result != CHTTPX_OK)
        logger_error("http_init: cHTTPX_AppRun failed, error=%d", run_result);

    _http_cleanup();
}

static chttpx_config_t _server_config(uint16_t port, const char** languages, size_t languages_count)
{
    chttpx_config_t config = cHTTPX_DefaultConfig();
    config.port = port;
    config.max_clients = 8192;
    config.read_timeout_sec = 60;
    config.write_timeout_sec = 60;
    config.idle_timeout_sec = 90;
    config.max_header_size = 16 * 1024;
    config.max_body_size = 10 * 1024 * 1024;
    config.max_upload_size = 500ULL * 1024 * 1024;
    config.request_id_enabled = true;
    config.languages = languages;
    config.languages_count = languages_count;
    config.default_language = "en";
    config.log_level = CHTTPX_LOG_INFO;
    config.logger = logger_httpx;
    return config;
}

static void _http_cleanup(void)
{
    if (!app_context)
        return;

    rabbitmq_runtime_stop(app_context->rabbitmq);
    app_context->rabbitmq = NULL;

    if (app_context->app_initialized)
    {
        cHTTPX_AppShutdown(&app_context->app);
        app_context->app_initialized = false;
        app_context->main = NULL;
        app_context->auth = NULL;
        app_context->moderation = NULL;
    }

    MMDB_close(&app_context->geoip);

    if (app_context->conn)
    {
        db_close(app_context->conn);
        app_context->conn = NULL;
    }

    redis_disconnect();
    curl_global_cleanup();

    free(app_context);
    app_context = NULL;
}

static void _cors(chttpx_serv_t* server)
{
    if (!server)
        return;

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

    const char** allowed_origins = NULL;
    size_t origins_count = 0;

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

    cHTTPX_Cors(server, allowed_origins, origins_count, NULL,
                "Content-Type, Authorization, Accept-Language, X-Debug, Pow-Challenge, Pow-Nonce, X-Real-IP, X-Forwarded-For, X-Forwarded-Proto, "
                "Upgrade, Connection, Host");
}
