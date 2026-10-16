#ifndef POSTGRES_USERS_H
#define POSTGRES_USERS_H

#include "postgres/postgres.h"

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
} db_user_core_t;

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
} db_user_profile_t;

typedef struct {
    uint64_t id;
    time_t created_at;
    time_t updated_at;
    uint64_t user_uid;
    bool email_visible;
    bool username_visible;
    bool phone_visible;
} db_user_profile_access_t;

typedef struct {
    uint64_t id;
    time_t created_at;
    time_t updated_at;
    uint64_t user_uid;
} db_user_active_t;

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
} db_user_profile_update_t;

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
} db_user_info_t;

typedef struct {
    uint64_t id;
    char *username;
    char *avatar;
    uint64_t user_uid;
    char *email;
    char *last_message;
    time_t *last_message_date;
    uint16_t unread_message_count;
} db_user_minimal_with_l_message_t;

typedef struct {
    uint64_t id;
    time_t created_at;
    time_t updated_at;
    uint64_t user_uid;
    char *avatar;
    char *username;
    char *email;
    bool email_confirm;
} db_user_minimal_t;
/* ------------ */
/* table. USERS */

/* SQLs */
/* Create user_core */
db_result_t db_user_core_create(db_t* db, db_user_core_t* user);
/* Create user_profile */
db_result_t db_user_profile_create(db_t* db, db_user_profile_t* user);
/* Create user_profile_access */
db_result_t db_user_profile_access_create(db_t* db, db_user_profile_access_t* user);
/* Create user_profile_active */
db_result_t db_user_profile_active_create(db_t* db, db_user_active_t* user);
/* Create FULL user */
db_result_t db_user_create(db_t* db, db_user_core_t* user_core, db_user_profile_t* user_profile, 
                                db_user_profile_access_t* user_profile_access, db_user_active_t* user_active);
/* Get user info by user_uid */
db_result_t db_user_info_get_by_uid(db_t* db, uint64_t user_uid, db_user_info_t** out_user);
/* HELPER. Free user info */
void db_user_info_free(db_user_info_t* user);
/* Get user_core by email */
db_result_t db_user_core_lookup_by_email(db_t* db, char* email, db_user_core_t** out_user);
/* Get user_core by user_uid */
db_result_t db_user_core_get_by_uid(db_t* db, uint64_t user_uid, db_user_core_t** out_user);
/* Update user email confirm by email */
db_result_t db_user_core_upd_email_confirm(db_t* db, bool confirm, char* email);
/* Update user profile avatar by user_uid*/
db_result_t db_user_profile_upd_avatar_by_uid(db_t* db, uint64_t user_uid, char* avatar);
/* Update user profile access by user_uid */
db_result_t db_user_profile_access_upd_by_uid(db_t* db, uint64_t user_uid, db_user_profile_update_t* user);
/* Update user profile by user_uid */
db_result_t db_user_profile_upd_by_uid(db_t* db, uint64_t user_uid, db_user_profile_update_t* user);
/* Update user core by user_uid */
db_result_t db_user_core_upd_by_uid(db_t* db, uint64_t user_uid, db_user_profile_update_t* user);
/* Update user UPDATE ALL by user_uid */
db_result_t db_user_UPDATE_upd(db_t* db, uint64_t user_uid, db_user_profile_update_t* user);
/* Delete user by user_uid */
db_result_t db_user_del_by_uid(db_t* db, uint64_t user_uid);

#endif