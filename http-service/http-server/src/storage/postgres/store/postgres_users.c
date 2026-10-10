#include "postgres/postgres.h"
#include "postgres/postgres_users.h"

#include "logger.h"

db_result_t db_user_core_create(db_t* db, user_core_t* user)
{
    const char* query = "INSERT INTO user_cores (user_uid, email, password, email_confirm) VALUES ($1, $2, $3, $4)";

    /* convert -> char */
    char user_uid_str[32];
    snprintf(user_uid_str, sizeof(user_uid_str), "%lu", user->user_uid);

    const char* email_confirm_str = user->email_confirm ? "true" : "false";

    const char* params[4] = {user_uid_str, user->email, user->password, email_confirm_str};

    db_result_t result = execute_sql(db, query, params, 4);
    if (result != DB_OK)
    {
        logger_error("db_user_core_create user_uid={%lu}: failed to exec sql: %s", user->user_uid ? user->user_uid : 0, db_error_message(db));
    }

    return result;
}

db_result_t db_user_profile_create(db_t* db, user_profile_t* user)
{
    const char* query = "INSERT INTO user_profiles (user_uid, avatar, name, username) VALUES ($1, $2, $3, $4)";

    /* convert -> char */
    char user_uid_str[32];
    snprintf(user_uid_str, sizeof(user_uid_str), "%lu", user->user_uid);

    const char* params[4] = {user_uid_str, user->avatar, user->name, user->username};

    db_result_t result = execute_sql(db, query, params, 4);
    if (result != DB_OK)
    {
        logger_error("db_user_profile_create user_uid={%lu}: failed to exec sql: %s", user->user_uid ? user->user_uid : 0, db_error_message(db));
    }

    return result;
}

db_result_t db_user_profile_access_create(db_t* db, user_profile_access_t* user)
{
    const char* query = "INSERT INTO user_profile_accesses (user_uid, email_visible, "
                        "username_visible, phone_visible) VALUES ($1, $2, $3, $4)";

    /* convert -> char */
    char user_uid_str[32];
    snprintf(user_uid_str, sizeof(user_uid_str), "%lu", user->user_uid);

    const char* params[4] = {user_uid_str, user->email_visible ? "true" : "false", user->username_visible ? "true" : "false",
                             user->phone_visible ? "true" : "false"};

    db_result_t result = execute_sql(db, query, params, 4);
    if (result != DB_OK)
    {
        logger_error("db_user_profile_access_create user_uid={%lu}: failed to exec sql: %s", user->user_uid ? user->user_uid : 0,
                     db_error_message(db));
    }

    return result;
}

db_result_t db_user_profile_active_create(db_t* db, user_active_t* user)
{
    const char* query = "INSERT INTO user_actives (user_uid) VALUES ($1)";

    /* convert -> char */
    char user_uid_str[32];
    snprintf(user_uid_str, sizeof(user_uid_str), "%lu", user->user_uid);

    const char* params[1] = {user_uid_str};

    db_result_t result = execute_sql(db, query, params, 1);
    if (result != DB_OK)
    {
        logger_error("db_user_profile_active_create user_uid={%lu}: failed to exec sql: %s", user->user_uid ? user->user_uid : 0,
                     db_error_message(db));
    }

    return result;
}

typedef struct {
    user_core_t* user_core;
    user_profile_t* user_profile;
    user_profile_access_t* user_profile_access;
    user_active_t* user_active;
} db_user_create_args_t;

static db_result_t db_user_create_tx(db_t* db, void* arg)
{
    db_user_create_args_t* args = arg;
    db_result_t rc;

    rc = db_user_core_create(db, args->user_core);
    if (rc != DB_OK)
        return rc;

    rc = db_user_profile_create(db, args->user_profile);
    if (rc != DB_OK)
        return rc;

    rc = db_user_profile_access_create(db, args->user_profile_access);
    if (rc != DB_OK)
        return rc;

    return db_user_profile_active_create(db, args->user_active);
}

db_result_t db_user_create(db_t* db, user_core_t* user_core, user_profile_t* user_profile, user_profile_access_t* user_profile_access, user_active_t* user_active)
{
    db_user_create_args_t args = {
        .user_core = user_core,
        .user_profile = user_profile,
        .user_profile_access = user_profile_access,
        .user_active = user_active,
    };

    db_result_t rc = db_transaction(db, db_user_create_tx, &args);
    if (rc != DB_OK)
        logger_error("db_user_create: failed to exec sql: %s", db_error_message(db));

    return rc;
}

user_info_t* db_user_info_get_by_uid(db_t* db, uint64_t user_uid)
{
    db_result_set_t* rc = NULL;

    const char* query = "SELECT\n"
                        "    user_cores.id,\n"
                        "    user_cores.created_at,\n"
                        "    user_cores.user_uid,\n"
                        "    user_profiles.avatar,\n"
                        "    user_profiles.name,\n"
                        "    user_profiles.username,\n"
                        "    user_profile_accesses.username_visible,\n"
                        "    user_cores.email,\n"
                        "    user_profile_accesses.email_visible,\n"
                        "    user_cores.email_confirm,\n"
                        "    user_profiles.phone,\n"
                        "    user_profile_accesses.phone_visible,\n"
                        "    user_profiles.overview\n"
                        "FROM user_cores\n"
                        "LEFT JOIN user_profiles ON user_cores.user_uid = user_profiles.user_uid\n"
                        "LEFT JOIN user_profile_accesses ON user_cores.user_uid = user_profile_accesses.user_uid\n"
                        "WHERE user_cores.user_uid = $1\n"
                        "LIMIT 1";

    /* convert -> char */
    char user_uid_str[32];
    snprintf(user_uid_str, sizeof(user_uid_str), "%lu", user_uid);

    const char* params[1] = {user_uid_str};

    db_result_t result = execute_select(db, query, params, 1, &rc);
    if (result != DB_OK)
    {
        logger_error("db_user_info_get_by_uid user_uid={%lu}: failed to exec sql: %s", user_uid, db_error_message(db));
        free_result_set(rc);
        return NULL;
    }

    if (!rc || rc->n_rows == 0)
    {
        free_result_set(rc);
        return NULL;
    }

    user_info_t* user = malloc(sizeof(user_info_t));
    if (!user)
    {
        logger_error("db_user_info_get_by_uid user_uid={%lu}: calloc failed for user", user_uid);

        fprintf(stderr, "malloc failed\n");

        free_result_set(rc);
        return NULL;
    }

    memset(user, 0, sizeof(user_info_t));

    user->id = strtoull(rc->rows[0].columns[0], NULL, 10);
    user->created_at = parse_pg_timestamp(rc->rows[0].columns[1]);
    user->user_uid = strtoull(rc->rows[0].columns[2], NULL, 10);
    user->avatar = parse_pg_strdup(rc->rows[0].columns[3]);
    user->name = parse_pg_strdup(rc->rows[0].columns[4]);
    user->username = parse_pg_strdup(rc->rows[0].columns[5]);
    user->username_visible = parse_pg_bool(rc->rows[0].columns[6]);
    user->email = parse_pg_strdup(rc->rows[0].columns[7]);
    user->email_visible = parse_pg_bool(rc->rows[0].columns[8]);
    user->email_confirm = parse_pg_bool(rc->rows[0].columns[9]);
    user->phone = parse_pg_strdup(rc->rows[0].columns[10]);
    user->phone_visible = parse_pg_bool(rc->rows[0].columns[11]);
    user->overview = parse_pg_strdup(rc->rows[0].columns[12]);

    free_result_set(rc);

    return user;
}

void db_user_info_free(user_info_t* user)
{
    if (!user)
        return;

    free(user->avatar);
    free(user->name);
    free(user->username);
    free(user->email);
    free(user->phone);
    free(user->overview);

    free(user);
}

user_core_t* db_user_core_get_by_email(db_t* db, char* email)
{
    db_result_set_t* rc = NULL;

    const char* query = "SELECT * FROM user_cores WHERE email = $1 LIMIT 1";
    const char* params[1] = {email};

    db_result_t result = execute_select(db, query, params, 1, &rc);
    if (result != DB_OK)
    {
        logger_error("db_user_core_get_by_email email={%s}: failed to exec sql: %s", email, db_error_message(db));
        free_result_set(rc);
        return NULL;
    }

    if (!rc || rc->n_rows == 0)
    {
        free_result_set(rc);
        return NULL;
    }

    user_core_t* user = malloc(sizeof(user_core_t));
    if (!user)
    {
        logger_error("db_user_core_get_by_email email={%s}: calloc failed for user", email);

        fprintf(stderr, "malloc failed\n");

        free_result_set(rc);
        return NULL;
    }

    memset(user, 0, sizeof(user_core_t));

    user->id = strtoull(rc->rows[0].columns[0], NULL, 10);
    user->created_at = parse_pg_timestamp(rc->rows[0].columns[1]);
    user->updated_at = parse_pg_timestamp(rc->rows[0].columns[2]);
    user->user_uid = strtoull(rc->rows[0].columns[3], NULL, 10);
    user->email = parse_pg_strdup(rc->rows[0].columns[4]);
    user->email_confirm = parse_pg_bool(rc->rows[0].columns[5]);
    user->password = parse_pg_strdup(rc->rows[0].columns[6]);

    free_result_set(rc);

    return user;
}

user_core_t* db_user_core_get_by_uid(db_t* db, uint64_t user_uid)
{
    db_result_set_t* rc = NULL;

    const char* query = "SELECT id, user_uid, email, email_confirm, password FROM user_cores "
                        "WHERE user_uid = $1 LIMIT 1";

    /* convert -> char */
    char user_uid_str[32];
    snprintf(user_uid_str, sizeof(user_uid_str), "%lu", user_uid);

    const char* params[1] = {user_uid_str};

    db_result_t result = execute_select(db, query, params, 1, &rc);
    if (result != DB_OK)
    {
        logger_error("db_user_core_get_by_uid user_uid={%lu}: failed to exec sql: %s", user_uid, db_error_message(db));
        free_result_set(rc);
        return NULL;
    }

    if (!rc || rc->n_rows == 0)
    {
        free_result_set(rc);
        return NULL;
    }

    user_core_t* user = malloc(sizeof(user_core_t));
    if (!user)
    {
        logger_error("db_user_core_get_by_uid user_uid={%lu}: malloc failed for user", user_uid);

        fprintf(stderr, "malloc failed\n");

        free_result_set(rc);
        return NULL;
    }

    memset(user, 0, sizeof(user_core_t));

    user->id = strtoull(rc->rows[0].columns[0], NULL, 10);
    user->user_uid = strtoull(rc->rows[0].columns[1], NULL, 10);
    user->email = parse_pg_strdup(rc->rows[0].columns[2]);
    user->email_confirm = parse_pg_bool(rc->rows[0].columns[3]);
    user->password = parse_pg_strdup(rc->rows[0].columns[4]);

    free_result_set(rc);

    return user;
}

db_result_t db_user_core_upd_email_confirm(db_t* db, bool confirm, char* email)
{
    const char* query = "UPDATE user_cores SET email_confirm = $1 WHERE email = $2";
    const char* params[2] = {confirm ? "true" : "false", email};

    db_result_t result = execute_sql(db, query, params, 2);
    if (result != DB_OK)
    {
        logger_error("db_user_core_upd_email_confirm email={%s}: failed to exec sql: %s", email, db_error_message(db));
    }

    return result;
}

db_result_t db_user_profile_upd_avatar_by_uid(db_t* db, uint64_t user_uid, char* avatar)
{
    const char* query = "UPDATE user_profiles SET avatar = $1 WHERE user_uid = $2";

    /* convert -> str */
    char user_uid_str[32];
    snprintf(user_uid_str, sizeof(user_uid_str), "%lu", user_uid);

    const char* params[2] = {avatar, user_uid_str};

    db_result_t result = execute_sql(db, query, params, 2);
    if (result != DB_OK)
    {
        logger_error("db_user_profile_upd_avatar_by_uid user_uid={%lu}: failed to exec sql: %s", user_uid, db_error_message(db));
    }

    return result;
}

db_result_t db_user_profile_access_upd_by_uid(db_t* db, uint64_t user_uid, user_profile_update_t* user)
{
    char query[512];
    const char* params[5];
    size_t param_count = 0;

    int i = 1;
    int first = 1;

    strcpy(query, "UPDATE user_profile_accesses SET ");

    if (user->username_visible)
    {
        snprintf(query + strlen(query), sizeof(query) - strlen(query), "%susername_visible=$%d", first ? "" : ", ", i++);
        params[param_count++] = user->username_visible ? "true" : "false";
        first = 0;
    }

    if (user->email_visible)
    {
        snprintf(query + strlen(query), sizeof(query) - strlen(query), "%semail_visible=$%d", first ? "" : ", ", i++);
        params[param_count++] = user->email_visible ? "true" : "false";
        first = 0;
    }

    if (user->phone_visible)
    {
        snprintf(query + strlen(query), sizeof(query) - strlen(query), "%sphone_visible=$%d", first ? "" : ", ", i++);
        params[param_count++] = user->phone_visible ? "true" : "false";
        first = 0;
    }

    if (param_count == 0)
        return DB_OK;

    char user_uid_str[32];
    snprintf(user_uid_str, sizeof(user_uid_str), "%lu", user_uid);

    snprintf(query + strlen(query), sizeof(query) - strlen(query), " WHERE user_uid=$%d", i);
    params[param_count++] = user_uid_str;

    db_result_t result = execute_sql(db, query, params, param_count);
    if (result != DB_OK)
    {
        logger_error("db_user_profile_access_upd_by_uid user_uid={%lu}: failed to exec sql: %s", user_uid, db_error_message(db));
    }

    return result;
}

db_result_t db_user_profile_upd_by_uid(db_t* db, uint64_t user_uid, user_profile_update_t* user)
{
    char query[512];
    const char* params[6];
    size_t param_count = 0;

    int i = 1;
    int first = 1;

    strcpy(query, "UPDATE user_profiles SET ");

    if (user->username)
    {
        snprintf(query + strlen(query), sizeof(query) - strlen(query), "%susername=$%d", first ? "" : ", ", i++);
        params[param_count++] = user->username;
        first = 0;
    }

    if (user->name)
    {
        snprintf(query + strlen(query), sizeof(query) - strlen(query), "%sname=$%d", first ? "" : ", ", i++);
        params[param_count++] = user->name;
        first = 0;
    }

    if (user->overview)
    {
        snprintf(query + strlen(query), sizeof(query) - strlen(query), "%soverview=$%d", first ? "" : ", ", i++);
        params[param_count++] = user->overview;
        first = 0;
    }

    if (user->phone)
    {
        snprintf(query + strlen(query), sizeof(query) - strlen(query), "%sphone=$%d", first ? "" : ", ", i++);
        params[param_count++] = user->phone;
        first = 0;
    }

    if (param_count == 0)
        return DB_OK;

    char user_uid_str[32];
    snprintf(user_uid_str, sizeof(user_uid_str), "%lu", user_uid);

    snprintf(query + strlen(query), sizeof(query) - strlen(query), " WHERE user_uid=$%d", i);
    params[param_count++] = user_uid_str;

    db_result_t result = execute_sql(db, query, params, param_count);
    if (result != DB_OK)
    {
        logger_error("db_user_profile_upd_by_uid user_uid={%lu}: failed to exec sql: %s", user_uid, db_error_message(db));
    }

    return result;
}

db_result_t db_user_core_upd_by_uid(db_t* db, uint64_t user_uid, user_profile_update_t* user)
{
    char query[512];
    const char* params[4];
    size_t param_count = 0;

    int i = 1;
    int first = 1;

    strcpy(query, "UPDATE user_cores SET ");

    if (user->email)
    {
        snprintf(query + strlen(query), sizeof(query) - strlen(query), "%semail=$%d", first ? "" : ", ", i++);
        params[param_count++] = user->email;
        first = 0;
    }

    if (user->password)
    {
        snprintf(query + strlen(query), sizeof(query) - strlen(query), "%spassword=$%d", first ? "" : ", ", i++);
        params[param_count++] = user->password;
        first = 0;
    }

    if (param_count == 0)
        return DB_OK;

    char user_uid_str[32];
    snprintf(user_uid_str, sizeof(user_uid_str), "%lu", user_uid);

    snprintf(query + strlen(query), sizeof(query) - strlen(query), " WHERE user_uid=$%d", i);
    params[param_count++] = user_uid_str;

    db_result_t result = execute_sql(db, query, params, param_count);
    if (result != DB_OK)
    {
        logger_error("db_user_core_upd_by_uid user_uid={%lu}: failed to exec sql: %s", user_uid, db_error_message(db));
    }

    return result;
}

typedef struct {
    uint64_t user_uid;
    user_profile_update_t* user;
} db_user_update_args_t;

static db_result_t db_user_update_tx(db_t* db, void* arg)
{
    db_user_update_args_t* args = arg;
    db_result_t rc;

    rc = db_user_core_upd_by_uid(db, args->user_uid, args->user);
    if (rc != DB_OK)
        return rc;

    rc = db_user_profile_upd_by_uid(db, args->user_uid, args->user);
    if (rc != DB_OK)
        return rc;

    return db_user_profile_access_upd_by_uid(db, args->user_uid, args->user);
}

db_result_t db_user_UPDATE_upd(db_t* db, uint64_t user_uid, user_profile_update_t* user)
{
    if (!db)
        return DB_ERROR;

    db_user_update_args_t args = {
        .user_uid = user_uid,
        .user = user,
    };

    db_result_t rc = db_transaction(db, db_user_update_tx, &args);
    if (rc != DB_OK)
        logger_error("db_user_UPDATE_upd user_uid={%lu}: failed to exec sql: %s", user_uid, db_error_message(db));

    return rc;
}

db_result_t db_user_del_by_uid(db_t* db, uint64_t user_uid)
{
    const char* query = "DELETE FROM user_cores WHERE user_uid = $1";

    char user_uid_str[32];
    snprintf(user_uid_str, sizeof(user_uid_str), "%lu", user_uid);

    const char* params[1] = {user_uid_str};

    db_result_t result = execute_sql(db, query, params, 1);
    if (result != DB_OK)
    {
        logger_error("db_user_del_by_uid user_uid={%lu}: failed to exec sql: %s", user_uid, db_error_message(db));
    }

    return result;
}