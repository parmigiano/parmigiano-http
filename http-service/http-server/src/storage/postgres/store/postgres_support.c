#include "postgres/postgres.h"
#include "postgres/postgres_support.h"

#include "logger.h"

db_result_t db_support_ticket_create(db_t* db, db_support_ticket_t* support_ticket)
{
    if (!db || !support_ticket)
        return DB_ERROR;

    const char* query = "INSERT INTO support_tickets (email, name, subject, description) "
                        "VALUES ($1, $2, $3, $4) RETURNING ticket_id, edit_token";

    const char* params[4] = {support_ticket->email, support_ticket->name, support_ticket->subject, support_ticket->description};

    db_result_set_t* rc = NULL;
    db_result_t result = execute_select(db, query, params, 4, &rc);
    if (result != DB_OK)
    {
        logger_error("db_support_ticket_create email={%s}: failed to exec sql: %s", support_ticket->email, db_error_message(db));
        free_result_set(rc);
        return result;
    }

    if (!rc || rc->n_rows == 0)
    {
        free_result_set(rc);
        return DB_ERROR;
    }

    support_ticket->ticket_id = parse_pg_strdup(rc->rows[0].columns[0]);
    support_ticket->edit_token = parse_pg_strdup(rc->rows[0].columns[1]);

    free_result_set(rc);

    if (!support_ticket->ticket_id || !support_ticket->edit_token)
    {
        free(support_ticket->ticket_id);
        free(support_ticket->edit_token);
        
        support_ticket->ticket_id = NULL;
        support_ticket->edit_token = NULL;

        return DB_ERROR;
    }

    return DB_OK;
}

db_result_t db_support_ticket_get_by_ticket_id(db_t* db, char* ticket_id, db_support_ticket_t** out_support_ticket)
{
    if (!db || !out_support_ticket || !ticket_id)
        return DB_ERROR;

    *out_support_ticket = NULL;
    db_result_set_t* rc = NULL;

    const char* query = "SELECT * FROM support_tickets WHERE ticket_id = $1 LIMIT 1";
    const char* params[1] = {ticket_id};

    db_result_t result = execute_select(db, query, params, 1, &rc);
    if (result != DB_OK)
    {
        logger_error("db_support_ticket_get_by_ticket_id ticket_id={%s}: failed to exec sql: %s", ticket_id, db_error_message(db));
        free_result_set(rc);
        return result;
    }

    if (!rc || rc->n_rows == 0)
    {
        free_result_set(rc);
        return DB_OK;
    }

    db_support_ticket_t* support_ticket = malloc(sizeof(db_support_ticket_t));
    if (!support_ticket)
    {
        logger_error("db_support_ticket_get_by_ticket_id ticket_id={%s}: malloc failed for support_ticket", ticket_id);

        fprintf(stderr, "malloc failed\n");

        free_result_set(rc);
        return DB_ERROR;
    }

    memset(support_ticket, 0, sizeof(db_support_ticket_t));

    support_ticket->id = strtoull(rc->rows[0].columns[0], NULL, 10);
    support_ticket->created_at = parse_pg_timestamp(rc->rows[0].columns[1]);
    support_ticket->updated_at = parse_pg_timestamp(rc->rows[0].columns[2]);
    support_ticket->ticket_id = parse_pg_strdup(rc->rows[0].columns[3]);
    support_ticket->email = parse_pg_strdup(rc->rows[0].columns[4]);
    support_ticket->name = parse_pg_strdup(rc->rows[0].columns[5]);
    support_ticket->subject = parse_pg_strdup(rc->rows[0].columns[6]);
    support_ticket->description = parse_pg_strdup(rc->rows[0].columns[7]);
    support_ticket->status = parse_pg_strdup(rc->rows[0].columns[8]);
    support_ticket->assigned_to = rc->rows[0].columns[9][0] == '\0' ? 0 : strtoull(rc->rows[0].columns[9], NULL, 10);
    support_ticket->answer = parse_pg_strdup(rc->rows[0].columns[10]);
    support_ticket->data_updated = parse_pg_bool(rc->rows[0].columns[11]);

    free_result_set(rc);

    *out_support_ticket = support_ticket;
    return DB_OK;
}

db_result_t db_support_ticket_get_by_ticket_id_and_token(db_t* db, char* ticket_id, char* edit_token, db_support_ticket_t** out_support_ticket)
{
    if (!db || !out_support_ticket || !ticket_id || !edit_token)
        return DB_ERROR;

    *out_support_ticket = NULL;
    db_result_set_t* rc = NULL;

    const char* query = "SELECT * FROM support_tickets WHERE ticket_id = $1 AND edit_token = $2 LIMIT 1";
    
    const char* params[2] = {ticket_id, edit_token};

    db_result_t result = execute_select(db, query, params, 2, &rc);
    if (result != DB_OK)
    {
        logger_error("db_support_ticket_get_by_ticket_id_and_token ticket_id={%s}: failed to exec sql: %s", ticket_id, db_error_message(db));
        free_result_set(rc);
        return result;
    }

    if (!rc || rc->n_rows == 0)
    {
        free_result_set(rc);
        return DB_OK;
    }

    db_support_ticket_t* support_ticket = malloc(sizeof(db_support_ticket_t));
    if (!support_ticket)
    {
        logger_error("db_support_ticket_get_by_ticket_id_and_token ticket_id={%s}: malloc failed for support_ticket", ticket_id);
        fprintf(stderr, "malloc failed\n");
        free_result_set(rc);
        return DB_ERROR;
    }

    memset(support_ticket, 0, sizeof(db_support_ticket_t));

    support_ticket->id = strtoull(rc->rows[0].columns[0], NULL, 10);
    support_ticket->created_at = parse_pg_timestamp(rc->rows[0].columns[1]);
    support_ticket->updated_at = parse_pg_timestamp(rc->rows[0].columns[2]);
    support_ticket->ticket_id = parse_pg_strdup(rc->rows[0].columns[3]);
    support_ticket->email = parse_pg_strdup(rc->rows[0].columns[4]);
    support_ticket->name = parse_pg_strdup(rc->rows[0].columns[5]);
    support_ticket->subject = parse_pg_strdup(rc->rows[0].columns[6]);
    support_ticket->description = parse_pg_strdup(rc->rows[0].columns[7]);
    support_ticket->status = parse_pg_strdup(rc->rows[0].columns[8]);
    support_ticket->assigned_to = rc->rows[0].columns[9][0] == '\0' ? 0 : strtoull(rc->rows[0].columns[9], NULL, 10);
    support_ticket->answer = parse_pg_strdup(rc->rows[0].columns[10]);
    support_ticket->data_updated = parse_pg_bool(rc->rows[0].columns[11]);

    free_result_set(rc);

    *out_support_ticket = support_ticket;
    return DB_OK;
}

db_result_t db_support_ticket_get_all(db_t* db, size_t offset, db_support_ticket_LIST_t** out_support_tickets)
{
    if (!db || !out_support_tickets)
        return DB_ERROR;

    *out_support_tickets = NULL;
    db_result_set_t* rc = NULL;

    const char* query = "SELECT * FROM support_tickets ORDER BY created_at, id LIMIT 15 OFFSET $1";

    char offset_str[32];
    snprintf(offset_str, sizeof(offset_str), "%zu", offset);

    const char* params[1] = {offset_str};

    db_result_t result = execute_select(db, query, params, 1, &rc);
    if (result != DB_OK)
    {
        logger_error("db_support_ticket_get_all offset={%zu}: failed to exec sql: %s", offset, db_error_message(db));
        free_result_set(rc);
        return result;
    }

    if (!rc || rc->n_rows == 0)
    {
        free_result_set(rc);
        return DB_OK;
    }

    db_support_ticket_LIST_t* list = malloc(sizeof(db_support_ticket_LIST_t));
    if (!list)
    {
        logger_error("db_support_ticket_get_all offset={%zu}: malloc failed for list", offset);

        fprintf(stderr, "malloc failed\n");

        free_result_set(rc);
        return DB_ERROR;
    }

    list->count = rc->n_rows;
    list->items = calloc(list->count, sizeof(db_support_ticket_t));
    if (!list->items)
    {
        logger_error("db_support_ticket_get_all offset={%zu}: calloc failed for list->items", offset);

        fprintf(stderr, "calloc failed\n");

        free(list);
        free_result_set(rc);
        return DB_ERROR;
    }

    for (size_t i = 0; i < rc->n_rows; i++)
    {
        db_support_ticket_t* support_ticket = &list->items[i];

        support_ticket->id = strtoull(rc->rows[i].columns[0], NULL, 10);
        support_ticket->created_at = parse_pg_timestamp(rc->rows[i].columns[1]);
        support_ticket->updated_at = parse_pg_timestamp(rc->rows[i].columns[2]);
        support_ticket->ticket_id = parse_pg_strdup(rc->rows[i].columns[3]);
        support_ticket->email = parse_pg_strdup(rc->rows[i].columns[4]);
        support_ticket->name = parse_pg_strdup(rc->rows[i].columns[5]);
        support_ticket->subject = parse_pg_strdup(rc->rows[i].columns[6]);
        support_ticket->description = parse_pg_strdup(rc->rows[i].columns[7]);
        support_ticket->status = parse_pg_strdup(rc->rows[i].columns[8]);
        support_ticket->assigned_to = rc->rows[i].columns[9][0] == '\0' ? 0 : strtoull(rc->rows[i].columns[9], NULL, 10);
        support_ticket->answer = parse_pg_strdup(rc->rows[i].columns[10]);
        support_ticket->data_updated = parse_pg_bool(rc->rows[i].columns[11]);
    }

    free_result_set(rc);

    *out_support_tickets = list;
    return DB_OK;
}

void db_support_ticket_free(db_support_ticket_t* support_ticket)
{
    if (!support_ticket)
        return;

    free(support_ticket->ticket_id);
    free(support_ticket->email);
    free(support_ticket->name);
    free(support_ticket->subject);
    free(support_ticket->description);
    free(support_ticket->status);
    free(support_ticket->answer);
    free(support_ticket->edit_token);

    free(support_ticket);
}

void db_support_ticket_list_free(db_support_ticket_LIST_t* support_tickets)
{
    if (!support_tickets)
        return;

    for (size_t i = 0; i < support_tickets->count; ++i)
    {
        free(support_tickets->items[i].ticket_id);
        free(support_tickets->items[i].email);
        free(support_tickets->items[i].name);
        free(support_tickets->items[i].subject);
        free(support_tickets->items[i].description);
        free(support_tickets->items[i].status);
        free(support_tickets->items[i].answer);
        free(support_tickets->items[i].edit_token);
    }

    free(support_tickets->items);
    free(support_tickets);
}

db_result_t db_support_ticket_update_status_by_ticket_id(db_t* db, char* ticket_id, char* status)
{
    const char* query = "UPDATE support_tickets SET status = $1 WHERE ticket_id = $2";
    const char* params[2] = {status, ticket_id};

    db_result_t result = execute_sql(db, query, params, 2);
    if (result != DB_OK)
    {
        logger_error("db_support_ticket_update_status_by_ticket_id ticket_id={%s}: failed to exec sql: %s", ticket_id, db_error_message(db));
    }

    return result;
}

db_result_t db_support_ticket_update_assigned_to_by_ticket_id(db_t* db, char* ticket_id, uint64_t assigned_to)
{
    const char* query = "UPDATE support_tickets SET assigned_to = CASE WHEN $1 = '0' THEN NULL ELSE $1::bigint END WHERE ticket_id = $2";

    char assigned_to_str[32];
    snprintf(assigned_to_str, sizeof(assigned_to_str), "%lu", assigned_to);

    const char* params[2] = {assigned_to_str, ticket_id};

    db_result_t result = execute_sql(db, query, params, 2);
    if (result != DB_OK)
    {
        logger_error("db_support_ticket_update_assigned_to_by_ticket_id ticket_id={%s}: failed to exec sql: %s", ticket_id, db_error_message(db));
    }

    return result;
}

db_result_t db_support_ticket_answer(db_t* db, char* ticket_id, char* answer, uint64_t assigned_to, db_support_ticket_t** out_support_ticket)
{
    if (!db || !ticket_id || !answer || !out_support_ticket)
        return DB_ERROR;

    *out_support_ticket = NULL;

    const char* query = "UPDATE support_tickets "
                        "SET answer = $1, status = 'in_progress', assigned_to = $2, data_updated = FALSE "
                        "WHERE ticket_id = $3 "
                        "RETURNING id, created_at, updated_at, ticket_id, email, name, subject, description, status, assigned_to, answer, data_updated, edit_token";

    char assigned_to_str[32];
    snprintf(assigned_to_str, sizeof(assigned_to_str), "%lu", assigned_to);

    const char* params[3] = {answer, assigned_to_str, ticket_id};

    db_result_set_t* rc = NULL;
    db_result_t result = execute_select(db, query, params, 3, &rc);
    if (result != DB_OK)
    {
        logger_error("db_support_ticket_answer ticket_id={%s}: failed to exec sql: %s", ticket_id, db_error_message(db));
        free_result_set(rc);
        return result;
    }

    if (!rc || rc->n_rows == 0)
    {
        free_result_set(rc);
        return DB_OK;
    }

    db_support_ticket_t* support_ticket = malloc(sizeof(db_support_ticket_t));
    if (!support_ticket)
    {
        logger_error("db_support_ticket_answer ticket_id={%s}: malloc failed for support_ticket", ticket_id);
        fprintf(stderr, "malloc failed\n");
        free_result_set(rc);
        return DB_ERROR;
    }

    memset(support_ticket, 0, sizeof(db_support_ticket_t));

    support_ticket->id = strtoull(rc->rows[0].columns[0], NULL, 10);
    support_ticket->created_at = parse_pg_timestamp(rc->rows[0].columns[1]);
    support_ticket->updated_at = parse_pg_timestamp(rc->rows[0].columns[2]);
    support_ticket->ticket_id = parse_pg_strdup(rc->rows[0].columns[3]);
    support_ticket->email = parse_pg_strdup(rc->rows[0].columns[4]);
    support_ticket->name = parse_pg_strdup(rc->rows[0].columns[5]);
    support_ticket->subject = parse_pg_strdup(rc->rows[0].columns[6]);
    support_ticket->description = parse_pg_strdup(rc->rows[0].columns[7]);
    support_ticket->status = parse_pg_strdup(rc->rows[0].columns[8]);
    support_ticket->assigned_to = rc->rows[0].columns[9][0] == '\0' ? 0 : strtoull(rc->rows[0].columns[9], NULL, 10);
    support_ticket->answer = parse_pg_strdup(rc->rows[0].columns[10]);
    support_ticket->data_updated = parse_pg_bool(rc->rows[0].columns[11]);
    support_ticket->edit_token = parse_pg_strdup(rc->rows[0].columns[12]);

    free_result_set(rc);

    *out_support_ticket = support_ticket;
    return DB_OK;
}

db_result_t db_support_ticket_append_description(db_t* db, char* ticket_id, char* edit_token, char* description, db_support_ticket_t** out_support_ticket)
{
    if (!db || !ticket_id || !edit_token || !description || !out_support_ticket)
        return DB_ERROR;

    *out_support_ticket = NULL;

    const char* query = "UPDATE support_tickets "
                        "SET description = description || E'\\n\\n' || $1, data_updated = TRUE "
                        "WHERE ticket_id = $2 AND edit_token = $3 "
                        "RETURNING id, created_at, updated_at, ticket_id, email, name, subject, description, status, assigned_to, answer, data_updated";

    const char* params[3] = {description, ticket_id, edit_token};

    db_result_set_t* rc = NULL;
    db_result_t result = execute_select(db, query, params, 3, &rc);
    if (result != DB_OK)
    {
        logger_error("db_support_ticket_append_description ticket_id={%s}: failed to exec sql: %s", ticket_id, db_error_message(db));
        free_result_set(rc);
        return result;
    }

    if (!rc || rc->n_rows == 0)
    {
        free_result_set(rc);
        return DB_OK;
    }

    db_support_ticket_t* support_ticket = malloc(sizeof(db_support_ticket_t));
    if (!support_ticket)
    {
        logger_error("db_support_ticket_append_description ticket_id={%s}: malloc failed for support_ticket", ticket_id);
        fprintf(stderr, "malloc failed\n");
        free_result_set(rc);
        return DB_ERROR;
    }

    memset(support_ticket, 0, sizeof(db_support_ticket_t));

    support_ticket->id = strtoull(rc->rows[0].columns[0], NULL, 10);
    support_ticket->created_at = parse_pg_timestamp(rc->rows[0].columns[1]);
    support_ticket->updated_at = parse_pg_timestamp(rc->rows[0].columns[2]);
    support_ticket->ticket_id = parse_pg_strdup(rc->rows[0].columns[3]);
    support_ticket->email = parse_pg_strdup(rc->rows[0].columns[4]);
    support_ticket->name = parse_pg_strdup(rc->rows[0].columns[5]);
    support_ticket->subject = parse_pg_strdup(rc->rows[0].columns[6]);
    support_ticket->description = parse_pg_strdup(rc->rows[0].columns[7]);
    support_ticket->status = parse_pg_strdup(rc->rows[0].columns[8]);
    support_ticket->assigned_to = rc->rows[0].columns[9][0] == '\0' ? 0 : strtoull(rc->rows[0].columns[9], NULL, 10);
    support_ticket->answer = parse_pg_strdup(rc->rows[0].columns[10]);
    support_ticket->data_updated = parse_pg_bool(rc->rows[0].columns[11]);

    free_result_set(rc);

    *out_support_ticket = support_ticket;
    return DB_OK;
}
