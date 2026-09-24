#ifndef POSTGRES_H
#define POSTGRES_H

#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <libpq-fe.h>

typedef enum {
    DB_OK = 0,
    DB_DUPLICATE = 1,
    DB_ERROR = -1,
    DB_TIMEOUT = -2
} db_result_t;

static inline char* parse_pg_strdup(const char* s)
{
    return s ? strdup(s) : NULL;
}

static inline bool parse_pg_is_leap_year(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

static inline int parse_pg_days_in_month(int year, int month)
{
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12)
        return 0;
    if (month == 2 && parse_pg_is_leap_year(year))
        return 29;
    return days[month - 1];
}

/* Convert a civil UTC date to days since 1970-01-01 without consulting the
 * process timezone. This keeps PostgreSQL UTC timestamps independent from the
 * host timezone/DST settings and avoids the non-portable `timezone` global. */
static inline int64_t parse_pg_days_from_civil(int year, unsigned month, unsigned day)
{
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned year_of_era = (unsigned)(year - era * 400);
    const unsigned shifted_month = month > 2 ? month - 3 : month + 9;
    const unsigned day_of_year = (153 * shifted_month + 2) / 5 + day - 1;
    const unsigned day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    return (int64_t)era * 146097 + (int64_t)day_of_era - 719468;
}

static inline time_t parse_pg_timestamp(const char* s)
{
    if (!s)
        return 0;

    int year, mon, day, hour, min, sec;
    if (sscanf(s, "%d-%d-%d %d:%d:%d", &year, &mon, &day, &hour, &min, &sec) != 6)
        return 0;

    if (year < 1 || mon < 1 || mon > 12 || day < 1 ||
        day > parse_pg_days_in_month(year, mon) ||
        hour < 0 || hour > 23 || min < 0 || min > 59 || sec < 0 || sec > 60)
        return 0;

    int64_t days = parse_pg_days_from_civil(year, (unsigned)mon, (unsigned)day);
    int64_t epoch = days * 86400 + (int64_t)hour * 3600 + (int64_t)min * 60 + sec;
    return (time_t)epoch;
}

static inline bool parse_pg_bool(const char* s)
{
    return s && (s[0] == 't' || s[0] == '1');
}

PGconn* db_conn(void);
void db_close(PGconn* conn);

typedef struct {
    char **columns;
    size_t n_columns;
} db_row_t;

typedef struct {
    db_row_t *rows;
    size_t n_rows;
} db_result_set_t;

db_result_t execute_sql(PGconn *conn, const char *query, const char **params, int n_params);
db_result_t execute_select(PGconn *conn, const char *query, const char **params, int n_params, db_result_set_t **out_result);
void free_result_set(db_result_set_t *rc);

PGresult* db_exec(PGconn* conn, const char* query);
PGresult* db_exec_params(PGconn* conn, const char* query, int n_params, const char** params);

#endif
