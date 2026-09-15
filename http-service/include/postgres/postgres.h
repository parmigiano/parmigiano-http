#ifndef POSTGRES_H
#define POSTGRES_H

#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <libpq-fe.h>

typedef enum {
    DB_OK = 0,
    DB_DUPLICATE = 1,
    DB_ERROR = -1,
    DB_TIMEOUT = -2
} db_result_t;

inline char* parse_pg_strdup(const char* s)
{
    return s ? strdup(s) : NULL;
}

inline time_t parse_pg_timestamp(const char* s)
{
    if (!s) return 0;

    int year, mon, day, hour, min, sec;
    if (sscanf(s, "%d-%d-%d %d:%d:%d", &year, &mon, &day, &hour, &min, &sec) != 6)
    {
        return 0;
    }

    struct tm tm = {0};
    tm.tm_year = year - 1900;
    tm.tm_mon  = mon - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min  = min;
    tm.tm_sec  = sec;

    return mktime(&tm) - timezone;
}

inline bool parse_pg_bool(const char* s)
{
    return s && (s[0] == 't' || s[0] == '1');
}

/* Connection to database */
PGconn* db_conn(void);

/* Disconnect from database */
void db_close(PGconn* conn);

/* Started migrations in tables */
void run_migrations(PGconn* conn);

/* One row SELECT result */
typedef struct {
    char **columns;
    size_t n_columns;
} db_row_t;

// many SELECT result
typedef struct {
    db_row_t *rows;
    size_t n_rows;
} db_result_set_t;

/* Exec INSERT/UPDATE/DELETE */
db_result_t execute_sql(PGconn *conn, const char *query, const char **params, int n_params);

/* Exec SELECT and return many lines -> out_result */
db_result_t execute_select(PGconn *conn, const char *query, const char **params, int n_params, db_result_set_t **out_result);

/* free memory SELECT */
void free_result_set(db_result_set_t *rc);

/* Thread-safe PQexec / PQexecParams wrappers */
PGresult* db_exec(PGconn* conn, const char* query);
PGresult* db_exec_params(PGconn* conn, const char* query, int n_params, const char** params);

#endif