#define _GNU_SOURCE

#include "db_internal.h"

#include "logger.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct
{
    PGconn* conn;
    int64_t created_ms;
    int64_t idle_since_ms;
    int in_use;
} db_slot_t;

struct db
{
    char* host;
    char* port;
    char* dbname;
    char* user;
    char* password;
    int max_open;
    int max_idle;
    int min_idle;
    int acquire_timeout_ms;
    int connect_timeout_ms;
    int statement_timeout_ms;
    int max_lifetime_sec;
    int max_idle_time_sec;

    pthread_mutex_t mu;
    pthread_cond_t cv;
    db_slot_t* slots;
    int open_count;
    int in_use;
    int closing;
};

typedef struct
{
    db_t* db;
    PGconn* conn;
    int slot;
    int depth;
    int discard;
} db_pin_t;

static _Thread_local db_pin_t tls_pin;
static _Thread_local db_result_t tls_last = DB_OK;
static _Thread_local char tls_err[768];

static int64_t mono_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void add_ms(struct timespec* ts, int ms)
{
    ts->tv_sec += ms / 1000;
    ts->tv_nsec += (long)(ms % 1000) * 1000000L;
    if (ts->tv_nsec >= 1000000000L)
    {
        ts->tv_sec += 1;
        ts->tv_nsec -= 1000000000L;
    }
}

static char* dup_str(const char* value)
{
    return strdup(value ? value : "");
}

static int message_is_timeout(const char* message)
{
    return message && strstr(message, "timeout") != NULL;
}

void db_set_last(db_result_t result, const char* message)
{
    tls_last = result;
    snprintf(tls_err, sizeof(tls_err), "%s", message ? message : "");
}

db_result_t db_last_result(void)
{
    return tls_last;
}

const char* db_error_message(db_t* db)
{
    (void)db;
    if (tls_err[0] != '\0')
        return tls_err;
    if (tls_pin.depth > 0 && tls_pin.conn)
        return PQerrorMessage(tls_pin.conn);
    return "";
}

static PGconn* db_connect_new(db_t* db, db_result_t* out_result)
{
    char timeout_sec[16];
    char options[64];
    int seconds = 0;

    if (db->connect_timeout_ms > 0)
    {
        seconds = (db->connect_timeout_ms + 999) / 1000;
        if (seconds < 2)
            seconds = 2;
        snprintf(timeout_sec, sizeof(timeout_sec), "%d", seconds);
    }

    options[0] = '\0';
    if (db->statement_timeout_ms > 0)
        snprintf(options, sizeof(options), "-c statement_timeout=%d", db->statement_timeout_ms);

    const char* keys[] = {
        "host", "port", "dbname", "user", "password", "connect_timeout", "options", NULL,
    };
    const char* values[] = {
        db->host, db->port[0] ? db->port : "5432", db->dbname, db->user, db->password, seconds > 0 ? timeout_sec : NULL, options[0] ? options : NULL,
        NULL,
    };

    PGconn* conn = PQconnectdbParams(keys, values, 0);
    if (!conn)
    {
        db_set_last(DB_ERROR, "database connection failed");
        logger_error("database connection failed");
        *out_result = DB_ERROR;
        return NULL;
    }

    if (PQstatus(conn) != CONNECTION_OK)
    {
        const char* message = PQerrorMessage(conn);
        db_result_t result = message_is_timeout(message) ? DB_TIMEOUT : DB_ERROR;
        db_set_last(result, message);
        logger_error("database connection error: %s", message);
        PQfinish(conn);
        *out_result = result;
        return NULL;
    }

    *out_result = DB_OK;
    return conn;
}

static void rollback_open(PGconn* conn)
{
    if (!conn || PQstatus(conn) != CONNECTION_OK)
        return;

    PGTransactionStatusType status = PQtransactionStatus(conn);
    if (status != PQTRANS_INTRANS && status != PQTRANS_INERROR && status != PQTRANS_ACTIVE)
        return;

    PGresult* result = PQexec(conn, "ROLLBACK");
    if (result)
        PQclear(result);
}

static int claim_empty_locked(db_t* db)
{
    if (db->open_count >= db->max_open)
        return -1;

    for (int i = 0; i < db->max_open; i++)
    {
        if (db->slots[i].conn || db->slots[i].in_use)
            continue;

        db->slots[i].in_use = 1;
        db->open_count++;
        db->in_use++;
        return i;
    }

    return -1;
}

static void abandon_reserved_locked(db_t* db, int slot)
{
    db->slots[slot].conn = NULL;
    db->slots[slot].in_use = 0;
    db->open_count--;
    db->in_use--;
    pthread_cond_broadcast(&db->cv);
}

static int connection_expired_locked(db_t* db, int slot, int64_t now)
{
    db_slot_t* item = &db->slots[slot];
    if (db->max_lifetime_sec > 0 && now - item->created_ms >= (int64_t)db->max_lifetime_sec * 1000)
        return 1;
    if (!item->in_use && db->max_idle_time_sec > 0 && now - item->idle_since_ms >= (int64_t)db->max_idle_time_sec * 1000)
        return 1;
    return 0;
}

static int take_idle_locked(db_t* db)
{
    for (int i = 0; i < db->max_open; i++)
    {
        if (!db->slots[i].conn || db->slots[i].in_use)
            continue;

        int64_t now = mono_ms();
        int drop = PQstatus(db->slots[i].conn) != CONNECTION_OK || connection_expired_locked(db, i, now);
        if (drop)
        {
            PGconn* conn = db->slots[i].conn;
            db->slots[i].conn = NULL;
            db->open_count--;
            pthread_mutex_unlock(&db->mu);
            PQfinish(conn);
            pthread_mutex_lock(&db->mu);
            return -2;
        }

        db->slots[i].in_use = 1;
        db->in_use++;
        return i;
    }

    return -1;
}

static db_result_t pool_acquire(db_t* db, int* out_slot)
{
    struct timespec deadline;
    clock_gettime(CLOCK_MONOTONIC, &deadline);
    if (db->acquire_timeout_ms > 0)
        add_ms(&deadline, db->acquire_timeout_ms);

    pthread_mutex_lock(&db->mu);

    for (;;)
    {
        if (db->closing)
        {
            pthread_mutex_unlock(&db->mu);
            db_set_last(DB_ERROR, "database pool is closed");
            return DB_ERROR;
        }

        int slot = take_idle_locked(db);
        if (slot == -2)
            continue;
        if (slot >= 0)
        {
            *out_slot = slot;
            pthread_mutex_unlock(&db->mu);
            return DB_OK;
        }

        slot = claim_empty_locked(db);
        if (slot >= 0)
        {
            pthread_mutex_unlock(&db->mu);

            db_result_t connect_result = DB_ERROR;
            PGconn* conn = db_connect_new(db, &connect_result);

            pthread_mutex_lock(&db->mu);
            if (!conn || db->closing)
            {
                abandon_reserved_locked(db, slot);
                pthread_mutex_unlock(&db->mu);
                if (conn)
                    PQfinish(conn);
                if (db->closing)
                {
                    db_set_last(DB_ERROR, "database pool is closed");
                    return DB_ERROR;
                }
                return connect_result;
            }

            db->slots[slot].conn = conn;
            db->slots[slot].created_ms = mono_ms();
            db->slots[slot].idle_since_ms = db->slots[slot].created_ms;
            *out_slot = slot;
            pthread_mutex_unlock(&db->mu);
            return DB_OK;
        }

        if (db->acquire_timeout_ms <= 0)
        {
            pthread_cond_wait(&db->cv, &db->mu);
            continue;
        }

        int wait_result = pthread_cond_timedwait(&db->cv, &db->mu, &deadline);
        if (wait_result != 0)
        {
            pthread_mutex_unlock(&db->mu);
            db_set_last(DB_TIMEOUT, "timed out waiting for a database connection");
            logger_error("db_acquire: timed out waiting for a database connection");
            return DB_TIMEOUT;
        }
    }
}

static void pool_release(db_t* db, int slot, int discard)
{
    PGconn* conn = db->slots[slot].conn;
    rollback_open(conn);

    int bad = !conn || PQstatus(conn) != CONNECTION_OK;

    pthread_mutex_lock(&db->mu);

    int64_t now = mono_ms();
    int expired = conn && connection_expired_locked(db, slot, now);
    db->slots[slot].in_use = 0;
    db->in_use--;

    int idle_after = db->open_count - db->in_use;
    if (discard || bad || expired || db->closing || idle_after > db->max_idle)
    {
        db->slots[slot].conn = NULL;
        if (conn)
            db->open_count--;
        pthread_cond_broadcast(&db->cv);
        pthread_mutex_unlock(&db->mu);
        if (conn)
            PQfinish(conn);
        return;
    }

    db->slots[slot].idle_since_ms = now;
    pthread_cond_broadcast(&db->cv);
    pthread_mutex_unlock(&db->mu);
}

static void pool_fill_idle(db_t* db)
{
    for (;;)
    {
        pthread_mutex_lock(&db->mu);
        int idle = db->open_count - db->in_use;
        if (db->closing || idle >= db->min_idle || db->open_count >= db->max_open)
        {
            pthread_mutex_unlock(&db->mu);
            return;
        }

        int slot = claim_empty_locked(db);
        if (slot < 0)
        {
            pthread_mutex_unlock(&db->mu);
            return;
        }
        pthread_mutex_unlock(&db->mu);

        db_result_t connect_result = DB_ERROR;
        PGconn* conn = db_connect_new(db, &connect_result);

        pthread_mutex_lock(&db->mu);
        if (!conn || db->closing)
        {
            abandon_reserved_locked(db, slot);
            pthread_mutex_unlock(&db->mu);
            if (conn)
                PQfinish(conn);
            return;
        }

        db->slots[slot].conn = conn;
        db->slots[slot].created_ms = mono_ms();
        db->slots[slot].idle_since_ms = db->slots[slot].created_ms;
        db->slots[slot].in_use = 0;
        db->in_use--;
        pthread_cond_broadcast(&db->cv);
        pthread_mutex_unlock(&db->mu);
    }
}

db_result_t db_checkout(db_t* db, PGconn** out_conn, int* out_slot, int* out_owned)
{
    if (!db || !out_conn || !out_slot || !out_owned)
    {
        db_set_last(DB_ERROR, "database is not open");
        return DB_ERROR;
    }

    if (tls_pin.depth > 0)
    {
        if (tls_pin.db != db || !tls_pin.conn || PQstatus(tls_pin.conn) != CONNECTION_OK)
        {
            const char* message = tls_pin.conn ? PQerrorMessage(tls_pin.conn) : "database connection is not ready";
            db_set_last(DB_ERROR, message);
            return DB_ERROR;
        }

        *out_conn = tls_pin.conn;
        *out_slot = tls_pin.slot;
        *out_owned = 0;
        return DB_OK;
    }

    int slot = -1;
    db_result_t result = pool_acquire(db, &slot);
    if (result != DB_OK)
        return result;

    *out_conn = db->slots[slot].conn;
    *out_slot = slot;
    *out_owned = 1;
    return DB_OK;
}

void db_checkin(db_t* db, int slot, int owned, int discard)
{
    if (!owned)
    {
        if (discard)
            tls_pin.discard = 1;
        return;
    }

    pool_release(db, slot, discard);
}

static db_result_t db_command(PGconn* conn, const char* query)
{
    PGresult* result = PQexec(conn, query);
    if (!result)
    {
        db_set_last(DB_ERROR, PQerrorMessage(conn));
        logger_error("db: database error: %s", PQerrorMessage(conn));
        return DB_ERROR;
    }

    ExecStatusType status = PQresultStatus(result);
    if (status == PGRES_COMMAND_OK || status == PGRES_TUPLES_OK)
    {
        PQclear(result);
        return DB_OK;
    }

    const char* sqlstate = PQresultErrorField(result, PG_DIAG_SQLSTATE);
    const char* message = PQresultErrorMessage(result);
    db_result_t code = (sqlstate && strcmp(sqlstate, "57014") == 0) ? DB_TIMEOUT : DB_ERROR;
    db_set_last(code, message);
    logger_error("db: database error: %s", message ? message : "");
    PQclear(result);
    return code;
}

static void pin_clear(void)
{
    tls_pin.db = NULL;
    tls_pin.conn = NULL;
    tls_pin.slot = -1;
    tls_pin.depth = 0;
    tls_pin.discard = 0;
}

db_result_t db_transaction(db_t* db, db_tx_fn fn, void* arg)
{
    if (!db || !fn)
    {
        db_set_last(DB_ERROR, "database is not open");
        return DB_ERROR;
    }

    if (tls_pin.depth > 0)
    {
        if (tls_pin.db != db)
            return DB_ERROR;
        tls_pin.depth++;
        db_result_t nested = fn(db, arg);
        tls_pin.depth--;
        return nested;
    }

    int slot = -1;
    db_result_t acquired = pool_acquire(db, &slot);
    if (acquired != DB_OK)
        return acquired;

    PGconn* conn = db->slots[slot].conn;
    db_result_t began = db_command(conn, "BEGIN");
    if (began != DB_OK)
    {
        pool_release(db, slot, PQstatus(conn) != CONNECTION_OK);
        return began;
    }

    if (db->statement_timeout_ms > 0)
    {
        char query[64];
        snprintf(query, sizeof(query), "SET LOCAL statement_timeout = %d", db->statement_timeout_ms);
        db_result_t timed = db_command(conn, query);
        if (timed != DB_OK)
        {
            rollback_open(conn);
            pool_release(db, slot, PQstatus(conn) != CONNECTION_OK);
            return timed;
        }
    }

    tls_pin.db = db;
    tls_pin.conn = conn;
    tls_pin.slot = slot;
    tls_pin.depth = 1;
    tls_pin.discard = 0;

    db_result_t result = fn(db, arg);
    int discard = tls_pin.discard || PQstatus(conn) != CONNECTION_OK;
    pin_clear();

    if (result == DB_OK)
    {
        db_result_t committed = db_command(conn, "COMMIT");
        if (committed != DB_OK)
            result = committed;
    }
    else
    {
        char saved[768];
        snprintf(saved, sizeof(saved), "%s", tls_err);
        db_result_t saved_code = tls_last;
        rollback_open(conn);
        tls_last = saved_code;
        snprintf(tls_err, sizeof(tls_err), "%s", saved);
    }

    if (PQstatus(conn) != CONNECTION_OK)
        discard = 1;

    pool_release(db, slot, discard);
    return result;
}

db_result_t db_hold(db_t* db, db_tx_fn fn, void* arg)
{
    if (!db || !fn)
    {
        db_set_last(DB_ERROR, "database is not open");
        return DB_ERROR;
    }

    if (tls_pin.depth > 0)
        return DB_ERROR;

    int slot = -1;
    db_result_t acquired = pool_acquire(db, &slot);
    if (acquired != DB_OK)
        return acquired;

    tls_pin.db = db;
    tls_pin.conn = db->slots[slot].conn;
    tls_pin.slot = slot;
    tls_pin.depth = 1;
    tls_pin.discard = 1;

    db_result_t result = fn(db, arg);
    PGconn* conn = tls_pin.conn;
    pin_clear();
    rollback_open(conn);
    pool_release(db, slot, 1);
    return result;
}

static int env_int(const char* name, int fallback)
{
    const char* value = getenv(name);
    if (!value || !value[0])
        return fallback;

    char* end = NULL;
    long parsed = strtol(value, &end, 10);
    if (end == value || *end || parsed < 0 || parsed > 100000)
        return fallback;
    return (int)parsed;
}

static int clamp_config(db_t* db)
{
    if (!db->host[0] || !db->dbname[0] || !db->user[0])
    {
        logger_error("db_open: DB_HOST, DB_NAME and DB_USER are required");
        return 0;
    }

    if (db->max_open < 1 || db->max_open > 4096)
    {
        logger_error("db_open: max_open must be between 1 and 4096");
        return 0;
    }

    if (db->max_idle > db->max_open)
        db->max_idle = db->max_open;
    if (db->min_idle > db->max_idle)
        db->min_idle = db->max_idle;
    return 1;
}

db_t* db_open(const db_config_t* config)
{
    if (!config)
    {
        logger_error("db_open: missing database config");
        return NULL;
    }

    db_t* db = calloc(1, sizeof(*db));
    if (!db)
        return NULL;

    db->host = dup_str(config->host);
    db->port = dup_str(config->port);
    db->dbname = dup_str(config->dbname);
    db->user = dup_str(config->user);
    db->password = dup_str(config->password);
    db->max_open = config->max_open;
    db->max_idle = config->max_idle;
    db->min_idle = config->min_idle;
    db->acquire_timeout_ms = config->acquire_timeout_ms;
    db->connect_timeout_ms = config->connect_timeout_ms;
    db->statement_timeout_ms = config->statement_timeout_ms;
    db->max_lifetime_sec = config->max_lifetime_sec;
    db->max_idle_time_sec = config->max_idle_time_sec;

    if (!db->host || !db->port || !db->dbname || !db->user || !db->password || !clamp_config(db))
    {
        db_close(db);
        return NULL;
    }

    db->slots = calloc((size_t)db->max_open, sizeof(db_slot_t));
    if (!db->slots)
    {
        db_close(db);
        return NULL;
    }

    pthread_mutex_init(&db->mu, NULL);

    pthread_condattr_t attr;
    pthread_condattr_init(&attr);
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    pthread_cond_init(&db->cv, &attr);
    pthread_condattr_destroy(&attr);

    int warm = db->min_idle > 0 ? db->min_idle : 1;
    int saved_min_idle = db->min_idle;
    db->min_idle = warm;
    pool_fill_idle(db);
    db->min_idle = saved_min_idle;
    if (db->open_count < warm)
    {
        logger_error("db_open: failed to open the minimum idle connections");
        db_close(db);
        return NULL;
    }

    logger_info("Database pool opened, idle=%d max_open=%d", db->open_count, db->max_open);
    return db;
}

db_t* db_open_env(void)
{
    db_config_t config = {
        .host = getenv("DB_HOST"),
        .port = getenv("DB_PORT"),
        .dbname = getenv("DB_NAME"),
        .user = getenv("DB_USER"),
        .password = getenv("DB_PASSWORD"),
        .max_open = env_int("DB_MAX_OPEN", 16),
        .max_idle = env_int("DB_MAX_IDLE", 8),
        .min_idle = env_int("DB_MIN_IDLE", 2),
        .acquire_timeout_ms = env_int("DB_ACQUIRE_TIMEOUT_MS", 3000),
        .connect_timeout_ms = env_int("DB_CONNECT_TIMEOUT_MS", 3000),
        .statement_timeout_ms = env_int("DB_STATEMENT_TIMEOUT_MS", 5000),
        .max_lifetime_sec = env_int("DB_MAX_LIFETIME_SEC", 1800),
        .max_idle_time_sec = env_int("DB_MAX_IDLE_TIME_SEC", 300),
    };

    return db_open(&config);
}

void db_close(db_t* db)
{
    if (!db)
        return;

    if (db->slots)
    {
        pthread_mutex_lock(&db->mu);
        db->closing = 1;
        pthread_cond_broadcast(&db->cv);
        while (db->in_use > 0)
            pthread_cond_wait(&db->cv, &db->mu);

        for (int i = 0; i < db->max_open; i++)
        {
            if (!db->slots[i].conn)
                continue;
            PQfinish(db->slots[i].conn);
            db->slots[i].conn = NULL;
        }

        db->open_count = 0;
        pthread_mutex_unlock(&db->mu);
        pthread_cond_destroy(&db->cv);
        pthread_mutex_destroy(&db->mu);
        free(db->slots);
    }

    free(db->host);
    free(db->port);
    free(db->dbname);
    free(db->user);
    free(db->password);
    free(db);
}
