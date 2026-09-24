#ifndef POSTGRES_USERS_H
#define POSTGRES_USERS_H

#include "postgres.h"

#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <libpq-fe.h>

/* table. USERS */
/* ------------ */
typedef struct {
    uint64_t id;
    time_t created_at;
    time_t updated_at;
    uint64_t user_uid;
    char *email;
    bool email_confirm;
    char *password;
} user_core_t;

typedef struct {
    uint64_t id;
    time_t created_at;
    time_t updated_at;
    uint64_t user_uid;
    char *avatar;
    char *name;
    char *username;
    char *overview;
    char *phone;
} user_profile_t;

typedef struct {
    uint64_t id;
    time_t created_at;
    time_t updated_at;
    uint64_t user_uid;
    bool email_visible;
    bool username_visible;
    bool phone_visible;
} user_profile_access_t;

typedef struct {
    uint64_t id;
    time_t created_at;
    time_t updated_at;
    uint64_t user_uid;
} user_active_t;

typedef struct {
    char *username;
    char *name;
    char *email;
    char *phone;
    char *overview;
    bool *username_visible;
    bool *phone_visible;
    bool *email_visible;
    char *password;
} user_profile_update_t;

typedef struct {
    uint64_t id;
    time_t created_at;
    uint64_t user_uid;
    char *avatar;
    char *name;
    char *username;
    bool username_visible;
    char *email;
    bool email_visible;
    bool email_confirm;
    char *phone;
    bool phone_visible;
    char *overview;
} user_info_t;

typedef struct {
    uint64_t id;
    char *username;
    char *avatar;
    uint64_t user_uid;
    char *email;
    char *last_message;
    time_t *last_message_date;
    uint16_t unread_message_count;
} user_minimal_with_l_message_t;

typedef struct {
    uint64_t id;
    time_t created_at;
    time_t updated_at;
    uint64_t user_uid;
    char *avatar;
    char *username;
    char *email;
    bool email_confirm;
} user_minimal_t;
/* ------------ */
/* table. USERS */

/* SQLs */
/* Create user_core */
db_result_t db_user_core_create(PGconn* conn, user_core_t* user);
/* Create user_profile */
db_result_t db_user_profile_create(PGconn* conn, user_profile_t* user);
/* Create user_profile_access */
db_result_t db_user_profile_access_create(PGconn* conn, user_profile_access_t* user);
/* Create user_profile_active */
db_result_t db_user_profile_active_create(PGconn* conn, user_active_t* user);
/* Create FULL user */
db_result_t db_user_create(PGconn* conn, user_core_t* user_core, user_profile_t* user_profile, 
                                user_profile_access_t* user_profile_access, user_active_t* user_active);
/* Get user info by user_uid */
user_info_t* db_user_info_get_by_uid(PGconn* conn, uint64_t user_uid);
/* HELPER. Free user info */
void db_user_info_free(user_info_t* user);
/* Get user_core by email */
user_core_t* db_user_core_get_by_email(PGconn* conn, char *email);
/* Get user_core by user_uid */
user_core_t* db_user_core_get_by_uid(PGconn* conn, uint64_t user_uid);
/* Update user email confirm by email */
db_result_t db_user_core_upd_email_confirm(PGconn* conn, bool confirm, char* email);
/* Update user profile avatar by user_uid*/
db_result_t db_user_profile_upd_avatar_by_uid(PGconn* conn, uint64_t user_uid, char* avatar);
/* Update user profile access by user_uid */
db_result_t db_user_profile_access_upd_by_uid(PGconn* conn, uint64_t user_uid, user_profile_update_t* user);
/* Update user profile by user_uid */
db_result_t db_user_profile_upd_by_uid(PGconn* conn, uint64_t user_uid, user_profile_update_t* user);
/* Update user core by user_uid */
db_result_t db_user_core_upd_by_uid(PGconn* conn, uint64_t user_uid, user_profile_update_t* user);
/* Update user UPDATE ALL by user_uid */
db_result_t db_user_UPDATE_upd(PGconn* conn, uint64_t user_uid, user_profile_update_t* user);
/* Delete user by user_uid */
db_result_t db_user_del_by_uid(PGconn* conn, uint64_t user_uid);

#endif