#include "postgres/postgres.h"

#include "logger.h"

#include <pthread.h>

static pthread_mutex_t pg_mutex = PTHREAD_MUTEX_INITIALIZER;

PGresult* db_exec(PGconn* conn, const char* query)
{
    pthread_mutex_lock(&pg_mutex);
    PGresult* result = PQexec(conn, query);
    pthread_mutex_unlock(&pg_mutex);
    return result;
}

PGresult* db_exec_params(PGconn* conn, const char* query, int n_params, const char** params)
{
    pthread_mutex_lock(&pg_mutex);
    PGresult* result = PQexecParams(conn, query, n_params, NULL, params, NULL, NULL, 0);
    pthread_mutex_unlock(&pg_mutex);
    return result;
}

db_result_t execute_sql(PGconn* conn, const char* query, const char** params, int n_params)
{
    if (!conn || !query)
        return DB_ERROR;

    int timeout_sec = 5;

    time_t start = time(NULL);

    while (1)
    {
        PGresult* result = db_exec_params(conn, query, n_params, params);
        if (!result)
            return DB_ERROR;

        ExecStatusType status = PQresultStatus(result);

        if (status == PGRES_COMMAND_OK)
        {
            PQclear(result);
            return DB_OK;
        }

        if (status == PGRES_FATAL_ERROR)
        {
            const char* err = PQresultErrorMessage(result);

            logger_error("execute_sql: database error: %s", err);
            PQclear(result);

            return DB_ERROR;
        }

        PQclear(result);

        if (difftime(time(NULL), start) > timeout_sec)
        {
            return DB_TIMEOUT;
        }
    }
}

db_result_t execute_select(PGconn* conn, const char* query, const char** params, int n_params, db_result_set_t** out_result)
{
    if (!conn || !query || !out_result)
        return DB_ERROR;

    int timeout_sec = 5;

    time_t start = time(NULL);

    while (1)
    {
        PGresult* result = db_exec_params(conn, query, n_params, params);
        if (!result)
            return DB_ERROR;

        ExecStatusType status = PQresultStatus(result);

        if (status == PGRES_TUPLES_OK)
        {
            size_t n_rows = PQntuples(result);
            size_t n_cols = PQnfields(result);

            db_result_set_t* result_set = malloc(sizeof(db_result_set_t));
            if (!result_set)
            {
                PQclear(result);
                return DB_ERROR;
            }

            result_set->n_rows = n_rows;
            result_set->rows = malloc(sizeof(db_row_t) * n_rows);
            if (!result_set->rows)
            {
                free(result_set);
                PQclear(result);
                return DB_ERROR;
            }

            for (size_t i = 0; i < n_rows; i++)
            {
                result_set->rows[i].n_columns = n_cols;
                result_set->rows[i].columns = malloc(sizeof(char*) * n_cols);
                if (!result_set->rows[i].columns)
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

                    return DB_ERROR;
                }

                for (size_t j = 0; j < n_cols; j++)
                {
                    const char* val = PQgetvalue(result, i, j);
                    result_set->rows[i].columns[j] = strdup(val ? val : "");
                }
            }

            PQclear(result);
            *out_result = result_set;

            return DB_OK;
        }

        if (status == PGRES_FATAL_ERROR)
        {
            PQclear(result);
            return DB_ERROR;
        }

        PQclear(result);

        if (difftime(time(NULL), start) > timeout_sec)
        {
            return DB_TIMEOUT;
        }
    }
}

void free_result_set(db_result_set_t* rc)
{
    if (!rc)
        return;

    for (size_t i = 0; i < rc->n_rows; i++)
    {
        for (size_t j = 0; j < rc->rows[i].n_columns; j++)
        {
            free(rc->rows[i].columns[j]);
        }

        free(rc->rows[i].columns);
    }

    free(rc->rows);
    free(rc);
}
