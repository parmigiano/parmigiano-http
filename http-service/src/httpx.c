#include "httpx.h"

#include "logger.h"
#include "routes.h"
#include "rabbitmq.h"
#include "utilities.h"
#include "middlewarex.h"
#include "redis/redis.h"
#include "postgres/postgres.h"

#include <time.h>
#include <errno.h>
#include <string.h>
#include <maxminddb.h>
#include <curl/curl.h>
#include <libchttpx/libchttpx.h>

httpx_server_t *http_server = NULL;

static void _cors(chttpx_serv_t* server);
static void _http_cleanup(void);

/* RabbitMQ prepare */
static void rabbitmq_workers_stop(httpx_server_t *server);
static bool rabbitmq_workers_start(httpx_server_t *server);

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

    if (!rabbitmq_workers_start(http_server))
    {
        logger_error("Failed to start RabbitMQ workers");
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
		return

	/* RabbitMQ */
    rabbitmq_workers_stop(http_server);

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

static void rabbitmq_retry_pause(httpx_rabbitmq_worker_t *worker)
{
	for (int i = 0; i < 50; ++i)
	{
		if (atomic_load(worker->stop))
			return;

		struct timespec remaining = {
            .tv_sec = 0,
            .tv_nsec = 100000000L /* 100 мс */
        };

		while (nanosleep(&remaining, &remaining) == -1) {
            if (errno != EINTR)
                return;

            if (atomic_load(worker->stop))
                return;
        }
	}
}

static rmq_action_t rabbitmq_dispatch(const rmq_message_t *message, void *userdata)
{
	httpx_rabbitmq_worker_t *worker = userdata;

    if (atomic_load(worker->stop)) {
        worker->last_action = RMQ_REQUEUE;
        return RMQ_REQUEUE;
    }

    worker->last_action = worker->handler(
        message,
        worker->handler_context
    );

    return worker->last_action;
}

static void *rabbitmq_worker_main(void *arg)
{
	httpx_rabbitmq_worker_t *worker = arg;

    rmq_config_t config = {
        .url = worker->url,
        .connect_timeout_ms = 3000,
        .rpc_timeout_ms = 5000,
        .heartbeat_seconds = 180,
        .tls_ca_file = NULL
    };

    while (!atomic_load(worker->stop)) {
        rmq_client_t *client = NULL;
        rmq_error_t error = {0};

        rmq_result_t result = rmq_connect(&client, &config, &error);

        if (result != RMQ_OK) {
            logger_error("RabbitMQ queue=%s operation=%s error=%s", worker->queue, error.operation, error.detail);

            rabbitmq_retry_pause(worker);
            continue;
        }

        result = rabbitmq_setup(client, &error);

        if (result == RMQ_OK && !atomic_load(worker->stop)) {
            result = rmq_subscribe(client, worker->queue, 1, &error);
        }

        if (result != RMQ_OK) {
            logger_error("RabbitMQ queue=%s operation=%s error=%s", worker->queue, error.operation, error.detail);

            rmq_disconnect(client);
            rabbitmq_retry_pause(worker);
            continue;
        }

        while (!atomic_load(worker->stop)) {
            worker->last_action = RMQ_ACK;

            result = rmq_consume_one(client, 1000, rabbitmq_dispatch, worker, &error);

            if (result == RMQ_IDLE)
                continue;

            if (result != RMQ_OK) {
                logger_error("RabbitMQ queue=%s operation=%s error=%s", worker->queue, error.operation, error.detail);
                break;
            }

            if (worker->last_action == RMQ_REQUEUE)
                break;
        }

        rmq_disconnect(client);

        if (!atomic_load(worker->stop))
            rabbitmq_retry_pause(worker);
    }

    return NULL;
}

static void rabbitmq_workers_stop(httpx_server_t *server)
{
    if (!server || !server->rabbitmq_url)
        return;

    atomic_store(&server->rabbitmq_stop, true);

    for (size_t i = 0; i < HTTPX_RABBITMQ_WORKERS; ++i) {
        httpx_rabbitmq_worker_t *worker = &server->rabbitmq_workers[i];

        if (!worker->started)
            continue;

        pthread_join(worker->thread, NULL);
        worker->started = false;
    }

    free(server->rabbitmq_url);
    server->rabbitmq_url = NULL;
}

static bool rabbitmq_workers_start(httpx_server_t *server)
{
    if (!server)
        return false;

    if (server->rabbitmq_url)
        return false;

    size_t count = 0;
    const rabbitmq_route_t* definitions = rabbitmq_routes(&count);

    if (count > HTTPX_RABBITMQ_WORKERS)
    {
        logger_error("RabbitMQ: too many routes (%zu), worker capacity is %d", count, HTTPX_RABBITMQ_WORKERS);
        return false;
    }

    const char *url = getenv("RABBITMQ_URL");

    if (!url || !*url) {
        logger_error("RabbitMQ: RABBITMQ_URL is empty");
        return false;
    }

    size_t url_size = strlen(url) + 1;

    server->rabbitmq_url = malloc(url_size);

    if (!server->rabbitmq_url) {
        logger_error("RabbitMQ: cannot allocate URL");
        return false;
    }

    memcpy(server->rabbitmq_url, url, url_size);

    atomic_init(&server->rabbitmq_stop, false);

    for (size_t i = 0; i < count; ++i) {
        httpx_rabbitmq_worker_t *worker = &server->rabbitmq_workers[i];

        worker->started = false;
        worker->queue = definitions[i].queue;
        worker->handler = definitions[i].handler;
        worker->handler_context = NULL;

        worker->url = server->rabbitmq_url;
        worker->stop = &server->rabbitmq_stop;
        worker->last_action = RMQ_ACK;

        int rc = pthread_create(&worker->thread, NULL, rabbitmq_worker_main, worker);

        if (rc != 0) {
            logger_error("RabbitMQ: cannot start worker queue=%s: %s", worker->queue, strerror(rc));

            rabbitmq_workers_stop(server);
            return false;
        }

        worker->started = true;
    }

    return true;
}
