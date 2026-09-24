#include "postgres/postgres.h"

#include "logger.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FOLDER_SQL_MIGRATIONS "./src/storage/postgres/migrations"
#define MIGRATION_LOCK_ID 73490127

static char* read_file_migrate(const char* path)
{
    FILE* f = fopen(path, "rb");
    if (!f)
        return NULL;

    if (fseek(f, 0, SEEK_END) != 0)
    {
        fclose(f);
        return NULL;
    }

    long size = ftell(f);
    if (size < 0)
    {
        fclose(f);
        return NULL;
    }

    rewind(f);

    char* data = malloc((size_t)size + 1);
    if (!data)
    {
        fclose(f);
        return NULL;
    }

    size_t read = fread(data, 1, (size_t)size, f);
    fclose(f);

    if (read != (size_t)size)
    {
        free(data);
        return NULL;
    }

    data[size] = '\0';
    return data;
}

static int parse_migration_version(const char* filename, int* out_version)
{
    if (!filename || !out_version || !isdigit((unsigned char)filename[0]))
        return 0;

    const char* p = filename;
    long version = 0;

    while (isdigit((unsigned char)*p))
    {
        version = version * 10 + (*p - '0');
        if (version > INT32_MAX)
            return 0;
        p++;
    }

    if (version <= 0 || *p != '_' || strlen(filename) < 5)
        return 0;

    size_t len = strlen(filename);
    if (len < 5 || strcmp(filename + len - 4, ".sql") != 0)
        return 0;

    *out_version = (int)version;
    return 1;
}

static int cmp_migration(const void* a, const void* b)
{
    const char* fa = *(const char* const*)a;
    const char* fb = *(const char* const*)b;

    int va = 0;
    int vb = 0;
    parse_migration_version(fa, &va);
    parse_migration_version(fb, &vb);

    if (va < vb)
        return -1;
    if (va > vb)
        return 1;
    return strcmp(fa, fb);
}

static void migration_checksum(const char* data, char out[17])
{
    uint64_t hash = 1469598103934665603ULL;

    for (const unsigned char* p = (const unsigned char*)data; *p; ++p)
    {
        hash ^= *p;
        hash *= 1099511628211ULL;
    }

    snprintf(out, 17, "%016llx", (unsigned long long)hash);
}

static int command_ok(PGresult* res)
{
    return res && PQresultStatus(res) == PGRES_COMMAND_OK;
}

static void migration_fatal(PGconn* conn, const char* message)
{
    logger_error("run_migrations: %s: %s", message, conn ? PQerrorMessage(conn) : "database unavailable");
    fprintf(stderr, "migration error: %s\n", message);

    if (conn)
    {
        PGresult* unlock = db_exec(conn, "SELECT pg_advisory_unlock(" "73490127" ")");
        if (unlock)
            PQclear(unlock);
    }

    exit(EXIT_FAILURE);
}

void run_migrations(PGconn* conn)
{
    if (!conn || PQstatus(conn) != CONNECTION_OK)
        migration_fatal(conn, "database connection is not ready");

    PGresult* res = db_exec(conn, "SELECT pg_advisory_lock(" "73490127" ")");
    if (!res || PQresultStatus(res) != PGRES_TUPLES_OK)
    {
        if (res)
            PQclear(res);
        migration_fatal(conn, "failed to acquire migration lock");
    }
    PQclear(res);

    res = db_exec(conn,
        "CREATE TABLE IF NOT EXISTS schema_migrations ("
        "version INT PRIMARY KEY, "
        "name TEXT, "
        "checksum VARCHAR(16), "
        "applied_at TIMESTAMPTZ NOT NULL DEFAULT now()"
        ")");
    if (!command_ok(res))
    {
        if (res)
            PQclear(res);
        migration_fatal(conn, "failed to initialize schema_migrations");
    }
    PQclear(res);

    res = db_exec(conn, "ALTER TABLE schema_migrations ADD COLUMN IF NOT EXISTS name TEXT");
    if (!command_ok(res))
    {
        if (res)
            PQclear(res);
        migration_fatal(conn, "failed to add migration name column");
    }
    PQclear(res);

    res = db_exec(conn, "ALTER TABLE schema_migrations ADD COLUMN IF NOT EXISTS checksum VARCHAR(16)");
    if (!command_ok(res))
    {
        if (res)
            PQclear(res);
        migration_fatal(conn, "failed to add migration checksum column");
    }
    PQclear(res);

    DIR* dir = opendir(FOLDER_SQL_MIGRATIONS);
    if (!dir)
    {
        char detail[256];
        snprintf(detail, sizeof(detail), "failed to open migrations directory %s: %s", FOLDER_SQL_MIGRATIONS, strerror(errno));
        migration_fatal(conn, detail);
    }

    size_t capacity = 32;
    size_t count = 0;
    char** files = calloc(capacity, sizeof(*files));
    if (!files)
    {
        closedir(dir);
        migration_fatal(conn, "out of memory while listing migrations");
    }

    struct dirent* ent;
    while ((ent = readdir(dir)) != NULL)
    {
        int version = 0;
        if (!parse_migration_version(ent->d_name, &version))
            continue;

        if (count == capacity)
        {
            capacity *= 2;
            char** grown = realloc(files, capacity * sizeof(*files));
            if (!grown)
            {
                closedir(dir);
                migration_fatal(conn, "out of memory while growing migration list");
            }
            files = grown;
        }

        files[count] = strdup(ent->d_name);
        if (!files[count])
        {
            closedir(dir);
            migration_fatal(conn, "out of memory while copying migration filename");
        }
        count++;
    }
    closedir(dir);

    qsort(files, count, sizeof(*files), cmp_migration);

    for (size_t i = 1; i < count; ++i)
    {
        int prev = 0;
        int current = 0;
        parse_migration_version(files[i - 1], &prev);
        parse_migration_version(files[i], &current);
        if (prev == current)
            migration_fatal(conn, "duplicate migration version detected");
    }

    for (size_t i = 0; i < count; ++i)
    {
        int version = 0;
        parse_migration_version(files[i], &version);

        char path[1024];
        int path_len = snprintf(path, sizeof(path), "%s/%s", FOLDER_SQL_MIGRATIONS, files[i]);
        if (path_len < 0 || (size_t)path_len >= sizeof(path))
            migration_fatal(conn, "migration path is too long");

        char* sql = read_file_migrate(path);
        if (!sql)
            migration_fatal(conn, "failed to read migration file");

        char checksum[17];
        migration_checksum(sql, checksum);

        char version_str[32];
        snprintf(version_str, sizeof(version_str), "%d", version);
        const char* check_params[1] = {version_str};

        res = db_exec_params(conn,
            "SELECT name, checksum FROM schema_migrations WHERE version = $1::int",
            1,
            check_params);

        if (!res || PQresultStatus(res) != PGRES_TUPLES_OK)
        {
            free(sql);
            if (res)
                PQclear(res);
            migration_fatal(conn, "failed to inspect migration state");
        }

        if (PQntuples(res) > 0)
        {
            const char* stored_checksum = PQgetisnull(res, 0, 1) ? NULL : PQgetvalue(res, 0, 1);
            PQclear(res);

            if (stored_checksum && strcmp(stored_checksum, checksum) != 0)
            {
                free(sql);
                migration_fatal(conn, "applied migration checksum mismatch");
            }

            if (!stored_checksum)
            {
                const char* update_params[3] = {files[i], checksum, version_str};
                res = db_exec_params(conn,
                    "UPDATE schema_migrations SET name = $1, checksum = $2 WHERE version = $3::int",
                    3,
                    update_params);
                if (!command_ok(res))
                {
                    free(sql);
                    if (res)
                        PQclear(res);
                    migration_fatal(conn, "failed to backfill migration metadata");
                }
                PQclear(res);
            }

            free(sql);
            free(files[i]);
            files[i] = NULL;
            continue;
        }
        PQclear(res);

        logger_info("Applying migration %s", files[i]);

        res = db_exec(conn, "BEGIN");
        if (!command_ok(res))
        {
            free(sql);
            if (res)
                PQclear(res);
            migration_fatal(conn, "failed to start migration transaction");
        }
        PQclear(res);

        res = db_exec(conn, sql);
        free(sql);

        if (!command_ok(res))
        {
            logger_error("run_migrations: migration %s failed: %s", files[i], PQerrorMessage(conn));
            if (res)
                PQclear(res);
            res = db_exec(conn, "ROLLBACK");
            if (res)
                PQclear(res);
            migration_fatal(conn, "migration execution failed");
        }
        PQclear(res);

        const char* insert_params[3] = {version_str, files[i], checksum};
        res = db_exec_params(conn,
            "INSERT INTO schema_migrations(version, name, checksum) VALUES($1::int, $2, $3)",
            3,
            insert_params);
        if (!command_ok(res))
        {
            if (res)
                PQclear(res);
            res = db_exec(conn, "ROLLBACK");
            if (res)
                PQclear(res);
            migration_fatal(conn, "failed to record migration");
        }
        PQclear(res);

        res = db_exec(conn, "COMMIT");
        if (!command_ok(res))
        {
            if (res)
                PQclear(res);
            migration_fatal(conn, "failed to commit migration");
        }
        PQclear(res);

        free(files[i]);
        files[i] = NULL;
    }

    free(files);

    res = db_exec(conn, "SELECT pg_advisory_unlock(" "73490127" ")");
    if (res)
        PQclear(res);
}
