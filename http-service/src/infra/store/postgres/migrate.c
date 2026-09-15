#include "postgres/postgres.h"

#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>

#define FOLDER_SQL_MIGRATIONS "./src/infra/store/postgres/migrations"

static char* read_file_migrate(const char* path)
{
    FILE* f = fopen(path, "rb");
    if (!f)
        return NULL;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);

    char* data = malloc(size + 1);
    fread(data, 1, size, f);
    data[size] = 0;

    fclose(f);
    return data;
}

static int cmp_migration(const void* a, const void* b)
{
    const char* fa = *(const char**)a;
    const char* fb = *(const char**)b;

    int va = atoi(fa);
    int vb = atoi(fb);

    return va - vb;
}

void run_migrations(PGconn* conn)
{
    PGresult* res = NULL;

    res = db_exec(conn, "CREATE TABLE IF NOT EXISTS schema_migrations ("
                         "version INT PRIMARY KEY, "
                         "applied_at TIMESTAMP DEFAULT now()"
                         ")");
    PQclear(res);

    DIR* dir = opendir(FOLDER_SQL_MIGRATIONS);
    if (!dir)
    {
        perror("opendir migrations");
        return;
    }

    struct dirent* ent;
    char* files[256];
    int count = 0;

    while ((ent = readdir(dir)))
    {
        if (strstr(ent->d_name, ".sql"))
        {
            files[count++] = strdup(ent->d_name);
        }
    }
    closedir(dir);

    qsort(files, count, sizeof(char*), cmp_migration);

    for (int i = 0; i < count; i++)
    {
        int version = atoi(files[i]);

        char check_sql[256];
        snprintf(check_sql, sizeof(check_sql), "SELECT 1 FROM schema_migrations WHERE version=%d", version);

        res = db_exec(conn, check_sql);
        if (PQntuples(res) > 0)
        {
            PQclear(res);
            free(files[i]);
            continue;
        }
        PQclear(res);

        char path[512];
        snprintf(path, sizeof(path), "%s/%s", FOLDER_SQL_MIGRATIONS, files[i]);

        char* sql = read_file_migrate(path);
        if (!sql)
        {
            free(files[i]);
            continue;
        }

        logger_info("Applying migration %s", files[i]);

        res = db_exec(conn, "BEGIN");
        PQclear(res);

        res = db_exec(conn, sql);
        free(sql);

        if (PQresultStatus(res) != PGRES_COMMAND_OK)
        {
            logger_error("run_migrations: migration failed: %s", PQerrorMessage(conn));

            fprintf(stderr, "migration failed: %s\n", PQerrorMessage(conn));
            PQclear(res);
            res = db_exec(conn, "ROLLBACK");
            PQclear(res);
            exit(1);
        }
        PQclear(res);

        char insert_sql[256];
        snprintf(insert_sql, sizeof(insert_sql), "INSERT INTO schema_migrations(version) VALUES(%d)", version);

        res = db_exec(conn, insert_sql);
        PQclear(res);

        res = db_exec(conn, "COMMIT");
        PQclear(res);

        free(files[i]);
    }
}
