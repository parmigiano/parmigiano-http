#include "httpx.h"

#include "logger.h"
#include "routes.h"
#include "utilities.h"
#include "middlewarex.h"
#include "redis/redis.h"
#include "postgres/postgres.h"

#include <string.h>
#include <maxminddb.h>
#include <curl/curl.h>
#include <libchttpx/libchttpx.h>

app_context_t *app_context = NULL;

static void _cors(chttpx_serv_t* server);
static void _http_cleanup(void);

void http_init(void)
{
    /* Initial logger */
    logger_init();

	app_context = (app_context_t*)calloc(1, sizeof(app_context_t));
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
	http_config.max_upload_size = 10 * 1024 * 1024;
	http_config.request_id_enabled = true;
	http_config.languages = languages;
	http_config.languages_count = CHTTPX_ARRAY_LEN(languages);
	http_config.default_language = "en";
	http_config.log_level = CHTTPX_LOG_INFO;
	http_config.logger = logger_httpx;

	app_context->http = cHTTPX_AppServer(&app_context->app, "auth", &http_config);
	if (!app_context->http)
	{
		logger_error("http_init: failed to create auth server");
		fprintf(stderr, "failed to create auth server\n");

		_http_cleanup();
		return;
	}
	logger_info("Auth server created on port %d", HTTPX_SERVER_PORT);

    /* Initial redis connect */
    if (!redis_conn())
    {
        logger_error("http_init: failed to start redis error");
        fprintf(stderr, "redis error\n");

		_http_cleanup();
		return;
    }

    /* Database connection (migrations are owned by http-server) */
    app_context->conn = db_conn();
    if (!app_context->conn)
    {
        logger_error("http_init: failed to connect to database");
        fprintf(stderr, "failed to connect to database\n");

		_http_cleanup();
		return;
    }

    /* Load in memory GeoIP */
    int status = MMDB_open("/usr/local/share/GeoIP/GeoLite2-Country.mmdb", MMDB_MODE_MMAP, &app_context->geoip);
    if (status != MMDB_SUCCESS)
    {
        logger_error("http_init: failed load GeoIP in memory: %s", MMDB_strerror(status));
        fprintf(stderr, "failed load GeoIP in memory: %s\n", MMDB_strerror(status));

		_http_cleanup();
		return;
    }

    /* Cors */
    _cors(app_context->http);

    /* Initial middlewares */
    cHTTPX_MiddlewareLogging(app_context->http);
    cHTTPX_MiddlewareRateLimiter(app_context->http, 5, 1);
    cHTTPX_MiddlewareUse(app_context->http, geoip_block_middleware);

    /* Initial routes */
    http_routes(app_context->http);

    int run_result = cHTTPX_AppRun(&app_context->app);
	if (run_result != CHTTPX_OK)
	{
		logger_error("http_init: cHTTPX_AppRun failed, error=%d", run_result);
	}

	_http_cleanup();
}

static void _http_cleanup(void)
{
	if (!app_context)
		return;

	if (app_context->app_initialized)
	{
		cHTTPX_AppShutdown(&app_context->app);

		app_context->app_initialized = false;
		app_context->http = NULL;
	}

	/* Free mmdb GeoIP */
    MMDB_close(&app_context->geoip);

	/* PostgreSQL */
	if (app_context->conn)
    {
        db_close(app_context->conn);
        app_context->conn = NULL;
    }

	/* Redis */
	redis_disconnect();

	/* Free CURL */
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

    const char* allowed_origins_dev[3] = {
        "http://localhost:8080",
        "http://localhost:8081",
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
