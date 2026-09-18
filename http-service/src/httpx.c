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

httpx_server_t *http_server = NULL;

static void _cors(chttpx_serv_t* server);
static void _http_cleanup(void);

void http_init(void)
{
    /* Initial logger */
    logger_init();

	http_server = (httpx_server_t*)calloc(1, sizeof(httpx_server_t));
    if (!http_server)
    {
        logger_error("http_init: calloc failed for http_server");
        fprintf(stderr, "calloc failed\n");
        return;
    }

	chttpx_error_t app_result = cHTTPX_AppInit(&http_server->app);
	if (app_result != CHTTPX_OK)
	{
		logger_error("http_init: failed to initialize cHTTPX App, error=%d", app_result);
		fprintf(stderr, "failed to initialize libchttpx app, error=%d\n", app_result);

		_http_cleanup();
		return;
	}
	http_server->app_initialized = true;

	/* Initial HTTP server / Config */
	static const char* languages[] = {
		"en", "ru",
	};

	chttpx_config_t http_config = cHTTPX_DefaultConfig();
	http_config.port = HTTPX_SERVER_PORT;
	http_config.max_clients = 8192;
	http_config.read_timeout_sec = 60;
	http_config.write_timeout_sec = 60;
	http_config.idle_timeout_sec = 90;
	http_config.max_header_size = 16 * 1024;
	http_config.max_body_size = 10 * 1024 * 1024;
	http_config.max_upload_size = 500ULL * 1024 * 1024;
	http_config.request_id_enabled = true;
	http_config.languages = languages;
	http_config.languages_count = CHTTPX_ARRAY_LEN(languages);
	http_config.default_language = "en";
	http_config.log_level = CHTTPX_LOG_INFO;
	http_config.logger = logger_httpx;

	http_server->http = cHTTPX_AppServer(&http_server->app, "http", &http_config);
	if (!http_server->http)
	{
		logger_error("http_init: failed to create http server");
		fprintf(stderr, "failed to create http server\n");

		_http_cleanup();
		return;
	}
	logger_info("HTTP server created on port %d", HTTPX_SERVER_PORT);

	chttpx_config_t moderation_config = cHTTPX_DefaultConfig();
	moderation_config.port = MODERATION_SERVER_PORT;
	moderation_config.max_clients = 8192;
	moderation_config.read_timeout_sec = 60;
	moderation_config.write_timeout_sec = 60;
	moderation_config.idle_timeout_sec = 90;
	moderation_config.max_header_size = 16 * 1024;
	moderation_config.max_body_size = 10 * 1024 * 1024;
	moderation_config.max_upload_size = 500ULL * 1024 * 1024;
	moderation_config.request_id_enabled = true;
	moderation_config.languages = languages;
	moderation_config.languages_count = CHTTPX_ARRAY_LEN(languages);
	moderation_config.default_language = "en";
	moderation_config.log_level = CHTTPX_LOG_INFO;
	moderation_config.logger = logger_httpx;

	http_server->moderation = cHTTPX_AppServer(&http_server->app, "moderation", &moderation_config);
	if (!http_server->moderation)
	{
		logger_error("http_init: failed to create moderation server");
		fprintf(stderr, "failed to create moderation server\n");

		_http_cleanup();
		return;
	}
	logger_info("Moderation server created on port %d", MODERATION_SERVER_PORT);

    /* Initial redis connect */
    if (!redis_conn())
    {
        logger_error("http_init: failed to start redis error");
        fprintf(stderr, "redis error\n");

		_http_cleanup();
		return;
    }

    /* Inital database, migrations */
    http_server->conn = db_conn();
    if (!http_server->conn)
    {
        logger_error("http_init: failed to connect to database");
        fprintf(stderr, "failed to connect to database\n");

		_http_cleanup();
		return;
    }

    run_migrations(http_server->conn);

    /* Load in memory GeoIP */
    int status = MMDB_open("/usr/local/share/GeoIP/GeoLite2-Country.mmdb", MMDB_MODE_MMAP, &http_server->geoip);
    if (status != MMDB_SUCCESS)
    {
        logger_error("http_init: failed load GeoIP in memory: %s", MMDB_strerror(status));
        fprintf(stderr, "failed load GeoIP in memory: %s\n", MMDB_strerror(status));

		_http_cleanup();
		return;
    }

    /* Initial AI queue */
    start_ai_worker();

    /* Cors */
    _cors(http_server->http);
    _cors(http_server->moderation);

    /* Initial middlewares */
    cHTTPX_MiddlewareLogging(http_server->http);
	cHTTPX_MiddlewareLogging(http_server->moderation);
    cHTTPX_MiddlewareRateLimiter(http_server->http, 5, 1);
    cHTTPX_MiddlewareUse(http_server->http, geoip_block_middleware);
    // cHTTPX_MiddlewareUse(email_confirmed_middleware);

    /* Initial routes */
    http_routes(http_server->http);
    moderation_routes(http_server->moderation);

    if (!rabbitmq_runtime_start(&http_server->rabbitmq))
    {
        logger_error("Failed to start RabbitMQ runtime");
    }

    int run_result = cHTTPX_AppRun(&http_server->app);
	if (run_result != CHTTPX_OK)
	{
		logger_error("http_init: cHTTPX_AppRun failed, error=%d", run_result);
	}

	_http_cleanup();
}

static void _http_cleanup(void)
{
	if (!http_server)
		return;

	/* RabbitMQ */
    rabbitmq_runtime_stop(http_server->rabbitmq);
    http_server->rabbitmq = NULL;

	if (http_server->app_initialized)
	{
		cHTTPX_AppShutdown(&http_server->app);

		http_server->app_initialized = false;
		http_server->http = NULL;
	}

	/* Free mmdb GeoIP */
    MMDB_close(&http_server->geoip);

	/* PostgreSQL */
	if (http_server->conn)
    {
        db_close(http_server->conn);
        http_server->conn = NULL;
    }

	/* Redis */
	redis_disconnect();

	/* Free CURL */
    curl_global_cleanup();

    free(http_server);
    http_server = NULL;
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
