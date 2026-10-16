#ifndef POSTGRES_SUPPORT_H
#define POSTGRES_SUPPORT_H

#include "postgres/postgres.h"

/* table. SUPPORT */
/* ------------ */
typedef struct {
    uint64_t id;
    time_t created_at;
    time_t updated_at;
    char* ticket_id;
    char* email;
    char* name;
    char* subject;
    char* description;
    char* status;
    uint64_t assigned_to;
    char* answer;
    bool data_updated;
    char* edit_token;
} db_support_ticket_t;

typedef struct {
    db_support_ticket_t* items;
    size_t count;
} db_support_ticket_LIST_t;
/* ------------ */
/* table. SUPPORT */

/* SQLs */
/* Create support ticket */
db_result_t db_support_ticket_create(db_t* db, db_support_ticket_t* support_ticket);
/* Get support ticket by ticket_id */
db_result_t db_support_ticket_get_by_ticket_id(db_t* db, char* ticket_id, db_support_ticket_t** out_support_ticket);
/* Get support ticket by ticket_id and edit token */
db_result_t db_support_ticket_get_by_ticket_id_and_token(db_t* db, char* ticket_id, char* edit_token, db_support_ticket_t** out_support_ticket);
/* Get all support tickets */
db_result_t db_support_ticket_get_all(db_t* db, size_t offset, db_support_ticket_LIST_t** out_support_tickets);
/* Update support ticket status by ticket_id */
db_result_t db_support_ticket_update_status_by_ticket_id(db_t* db, char* ticket_id, char* status);
/* Update support ticket assigned_to by ticket_id */
db_result_t db_support_ticket_update_assigned_to_by_ticket_id(db_t* db, char* ticket_id, uint64_t assigned_to);
/* Save an answer, assign the ticket and set status to in_progress */
db_result_t db_support_ticket_answer(db_t* db, char* ticket_id, char* answer, uint64_t assigned_to, db_support_ticket_t** out_support_ticket);
/* Append description when the edit token matches */
db_result_t db_support_ticket_append_description(db_t* db, char* ticket_id, char* edit_token, char* description, db_support_ticket_t** out_support_ticket);
/* HELPER. Free support ticket */
void db_support_ticket_free(db_support_ticket_t* support_ticket);
/* HELPER. Free support ticket list */
void db_support_ticket_list_free(db_support_ticket_LIST_t* support_tickets);

#endif