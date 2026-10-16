#ifndef POSTGRES_CHATS_H
#define POSTGRES_CHATS_H

#include "postgres/postgres.h"

/* table. CHATS */
/* ------------ */
typedef struct {
    uint64_t id;
    time_t created_at;
    time_t updated_at;
    char* chat_type; /* private ; group ; channel */
    char* title;
    char* description;
} db_chat_t;

typedef struct {
    uint64_t id;
    time_t created_at;
    uint64_t chat_id;
    uint64_t user_uid;
    char* role; /* owner ; admin ; member */
} db_chat_member_t;

typedef struct {
    uint64_t id;
    time_t created_at;
    time_t updated_at;
    uint64_t chat_id;
    char* custom_background;
    bool blocked;
    uint64_t who_blocked_uid;
} db_chat_setting_t;

typedef struct {
    uint64_t id;
    char* name;
    char* username;
    char* avatar;
    uint64_t user_uid;
    char* last_message;
    time_t last_message_date;
    uint16_t unread_message_count;
} db_chat_preview_t;

typedef struct {
    db_chat_preview_t* items;
    size_t count;
} db_chat_preview_LIST_t;

typedef struct {
    uint64_t* user_uids;
    size_t count;
} db_chat_member_LIST_t;
/* ------------ */
/* table. CHATS */

/* Create chat and return chat id */
db_result_t db_chat_create(db_t* db, db_chat_t* chat, uint64_t* out_chat_id);
/* Create chat member */
db_result_t db_chat_create_members(db_t* db, uint64_t chat_id, db_chat_member_t* members, size_t members_count);
/* Create chat setting */
db_result_t db_chat_create_setting(db_t* db, uint64_t chat_id, db_chat_setting_t* setting);
/* Create chat WITH transaction */
db_result_t db_chat_create_all(db_t* db, db_chat_t* chat, db_chat_member_t* members, size_t members_count, db_chat_setting_t* setting, uint64_t* out_chat_id);
/* Get my history chats [limit 15 chats] */
db_result_t db_chat_get_my_history(db_t* db, uint64_t user_uid, size_t offset, db_chat_preview_LIST_t** out_chats);
/* Get chats/users bu username */
db_result_t db_chat_get_by_username(db_t* db, uint64_t user_uid, const char* username, db_chat_preview_LIST_t** out_chats);
/* Get chat members by chat_id */
db_result_t db_chat_get_members_by_chat_id(db_t* db, uint64_t chat_id, uint64_t user_uid, db_chat_member_LIST_t** out_members);
/* Check exists or nah user in chat members by chat_id */
bool db_chat_get_member_exists_by_chat_id(db_t* db, uint64_t chat_id, uint64_t user_uid);
/* Get chat settings by chat_id */
db_result_t db_chat_get_setting_by_chat_id(db_t* db, uint64_t chat_id, db_chat_setting_t** out_setting);
/* Update chat custom background by chat_id */
db_result_t db_chat_upd_cbackground_by_chat_id(db_t* db, char* cbackground, uint64_t chat_id);

#endif