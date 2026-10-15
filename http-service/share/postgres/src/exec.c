#include "postgres/db_internal.h"

#include "logger.h"

#include <stdlib.h>
#include <string.h>

static int connection_failed(PGconn* conn)
{
    return !conn || PQstatus(conn) != CONNECTION_OK;
}

static db_result_t map_status(PGresult* result)
{
    return db_map_pg_result(result);
}

static PGresult* run_params(db_t* db, const char* query, int n_params, const char** params, int* discard)
{
    PGconn* conn = NULL;

    int slot = -1;
    int owned = 0;

    db_result_t checked = db_checkout(db, &conn, &slot, &owned);
    if (checked != DB_OK)
    {
        *discard = 0;
        return NULL;
    }

    PGresult* result = PQexecParams(conn, query, n_params, NULL, params, NULL, NULL, 0);
    *discard = connection_failed(conn);
    if (!result)
    {
        db_set_last(DB_ERROR, PQerrorMessage(conn));
        logger_error("execute_sql: database error: %s", PQerrorMessage(conn));
    }

    db_checkin(db, slot, owned, *discard);
    return result;
}

static PGresult* run_query(db_t* db, const char* query, int* discard)
{
    PGconn* conn = NULL;
    int slot = -1;
    int owned = 0;

    db_result_t checked = db_checkout(db, &conn, &slot, &owned);
    if (checked != DB_OK)
    {
        *discard = 0;
        return NULL;
    }

    PGresult* result = PQexec(conn, query);
    *discard = connection_failed(conn);
    if (!result)
    {
        db_set_last(DB_ERROR, PQerrorMessage(conn));
        logger_error("execute_sql: database error: %s", PQerrorMessage(conn));
    }

    db_checkin(db, slot, owned, *discard);
    return result;
}

PGresult* db_exec(db_t* db, const char* query)
{
    int discard = 0;
    return run_query(db, query, &discard);
}

PGresult* db_exec_params(db_t* db, const char* query, int n_params, const char** params)
{
    int discard = 0;
    return run_params(db, query, n_params, params, &discard);
}

db_result_t execute_sql(db_t* db, const char* query, const char** params, int n_params)
{
    if (!db || !query)
    {
        db_set_last(DB_ERROR, "database is not open");
        return DB_ERROR;
    }

    int discard = 0;
    PGresult* result = run_params(db, query, n_params, params, &discard);
    if (!result)
        return db_last_result() == DB_OK ? DB_ERROR : db_last_result();

    db_result_t code = map_status(result);
    if (code != DB_OK)
        logger_error("execute_sql: database error: %s", PQresultErrorMessage(result));

    PQclear(result);
    return code;
}

db_result_t execute_select(db_t* db, const char* query, const char** params, int n_params, db_result_set_t** out_result)
{
    if (!db || !query || !out_result)
    {
        db_set_last(DB_ERROR, "database is not open");
        return DB_ERROR;
    }

    int discard = 0;

    PGresult* result = run_params(db, query, n_params, params, &discard);
    if (!result)
        return db_last_result() == DB_OK ? DB_ERROR : db_last_result();

    if (PQresultStatus(result) != PGRES_TUPLES_OK)
    {
        db_result_t code = map_status(result);
        if (code != DB_OK)
            logger_error("execute_select: database error: %s", PQresultErrorMessage(result));

        PQclear(result);

        return code == DB_OK ? DB_ERROR : code;
    }

    size_t n_rows = PQntuples(result);
    size_t n_cols = PQnfields(result);

    db_result_set_t* result_set = malloc(sizeof(db_result_set_t));
    if (!result_set)
    {
        PQclear(result);
        db_set_last(DB_ERROR, "out of memory");
        return DB_ERROR;
    }

    result_set->n_rows = n_rows;
    result_set->rows = n_rows ? malloc(sizeof(db_row_t) * n_rows) : NULL;
    if (n_rows && !result_set->rows)
    {
        free(result_set);
        PQclear(result);
        db_set_last(DB_ERROR, "out of memory");
        return DB_ERROR;
    }

    for (size_t i = 0; i < n_rows; i++)
    {
        result_set->rows[i].n_columns = n_cols;
        result_set->rows[i].columns = n_cols ? malloc(sizeof(char*) * n_cols) : NULL;
        if (n_cols && !result_set->rows[i].columns)
        {
            for (size_t k = 0; k < i; k++)
            {
                for (size_t j = 0; j < result_set->rows[k].n_columns; j++)
                    free(result_set->rows[k].columns[j]);
                free(result_set->rows[k].columns);
            }

            free(result_set->rows);
            free(result_set);
            PQclear(result);

            db_set_last(DB_ERROR, "out of memory");

            return DB_ERROR;
        }

        for (size_t j = 0; j < n_cols; j++)
        {
            const char* val = PQgetvalue(result, i, j);
            result_set->rows[i].columns[j] = strdup(val ? val : "");
        }
    }

    PQclear(result);
    db_set_last(DB_OK, "");
    *out_result = result_set;

    return DB_OK;
}

void free_result_set(db_result_set_t* rc)
{
    if (!rc)
        return;

    for (size_t i = 0; i < rc->n_rows; i++)
    {
        for (size_t j = 0; j < rc->rows[i].n_columns; j++)
            free(rc->rows[i].columns[j]);

        free(rc->rows[i].columns);
    }

    free(rc->rows);
    free(rc);
}
