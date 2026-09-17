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

httpx_server_t* http_server = NULL;

static void _cors();
static void httpx_log(chttpx_log_level_t level, const char* request_id, const char* message, void* user_data);

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

    /* cHTTPX Server */
    chttpx_serv_t serv = {0};

	/* Initial HTTP server / Config */
	static const char* languages[] = {
		"en", "ru",
	};

	chttpx_config_t config = cHTTPX_DefaultConfig();
	config.port = HTTPX_SERVER_PORT;
	config.max_clients = 8192;
	config.read_timeout_sec = 60;
	config.write_timeout_sec = 60;
	config.idle_timeout_sec = 90;
	config.max_header_size = 16 * 1024;
	config.max_body_size = 10 * 1024 * 1024;
	config.max_upload_size = 500ULL * 1024 * 1024;
	config.request_id_enabled = true;
	config.languages = languages;
	config.languages_count = CHTTPX_ARRAY_LEN(languages);
	config.default_language = "en";
	config.log_level = CHTTPX_LOG_INFO;
	config.logger = httpx_log;

	chttpx_error_t httpx_init_result = cHTTPX_InitWithConfig(&serv, &config);

	if (httpx_init_result != CHTTPX_OK)
	{
		logger_error("http_init: failed to initialize libchttpx, error=%d", httpx_init_result);
		fprintf(stderr, "failed to initialize libchttpx, error=%d\n", httpx_init_result);

		free(http_server);
		http_server = NULL;

		return;
	}

    /* Initial redis connect */
    if (!redis_conn())
    {
        logger_error("http_init: failed to start redis error");
        fprintf(stderr, "redis error\n");

		cHTTPX_Shutdown();

		free(http_server);
		http_server = NULL;

		return;
    }

    /* Inital database, migrations */
    PGconn* conn = db_conn();
    if (!conn)
    {
        logger_error("http_init: failed to connect to database");
        fprintf(stderr, "failed to connect to database\n");

		redis_disconnect();
		cHTTPX_Shutdown();

		free(http_server);
		http_server = NULL;

		return;
    }
    http_server->conn = conn;

    run_migrations(conn);

    /* Load in memory GeoIP */
    int status = MMDB_open("/usr/local/share/GeoIP/GeoLite2-Country.mmdb", MMDB_MODE_MMAP, &http_server->geoip);
    if (status != MMDB_SUCCESS)
    {
        logger_error("http_init: failed load GeoIP in memory: %s", MMDB_strerror(status));
        fprintf(stderr, "failed load GeoIP in memory: %s\n", MMDB_strerror(status));

		db_close(http_server->conn);
		http_server->conn = NULL;

		redis_disconnect();
		cHTTPX_Shutdown();

		free(http_server);
		http_server = NULL;

		return;
    }

    /* Initial AI queue */
    start_ai_worker();

    /* Cors */
    _cors();

    /* Initial middlewares */
    cHTTPX_MiddlewareLogging();
    cHTTPX_MiddlewareRateLimiter(5, 1);
    cHTTPX_MiddlewareUse(geoip_block_middleware);
    // cHTTPX_MiddlewareUse(email_confirmed_middleware);

    /* Initial routes */
    routes();

    if (!rabbitmq_workers_start(http_server))
    {
        logger_error("Failed to start RabbitMQ workers");
    }

    /* At the very end, to start listening to incoming requests from users. */
    cHTTPX_Listen();

    /* Shutdown server */
    cHTTPX_Shutdown();

	/* RabbitMQ */
    rabbitmq_workers_stop(http_server);

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

    cHTTPX_Cors(allowed_origins, origins_count, NULL,
                "Content-Type, Authorization, Accept-Language, X-Debug, Pow-Challenge, Pow-Nonce, X-Real-IP, X-Forwarded-For, X-Forwarded-Proto, "
                "Upgrade, Connection, Host");
}

static void httpx_log(chttpx_log_level_t level, const char* request_id, const char* message, void* user_data)
{
    (void)user_data;
    const char* id = request_id && request_id[0] ? request_id : "-";
    const char* text = message ? message : "";

    switch (level)
    {
    case CHTTPX_LOG_ERROR:
        logger_error("libchttpx req={%s}: %s", id, text);
        break;
    case CHTTPX_LOG_WARN:
        logger_warn("libchttpx req={%s}: %s", id, text);
        break;
    case CHTTPX_LOG_DEBUG:
    case CHTTPX_LOG_INFO:
        logger_info("libchttpx req={%s}: %s", id, text);
        break;
    case CHTTPX_LOG_OFF:
        break;
    }
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
