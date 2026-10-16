#ifndef POSTGRES_GROUP_CHATS_H
#define POSTGRES_GROUP_CHATS_H

#include "postgres/postgres.h"

/* table. GROUP_CHATS */
/* ------------------ */
typedef struct {
    uint64_t id;
    time_t created_at;
    time_t updated_at;
    uint64_t user_uid;
    char* name;
} db_chat_group_t;

typedef struct {
    uint64_t id;
    uint64_t user_uid;
    uint64_t group_id;
    uint64_t chat_id;
} db_chat_group_user_chats_t;

typedef struct {
    uint64_t id;
    time_t created_at;
    uint64_t user_uid;
    char* name;
    uint64_t* chat_ids;
} db_chat_group_all_t;

typedef struct {
    db_chat_group_all_t* items;
    size_t count;
} db_chat_group_LIST_t;
/* ------------------ */
/* table. GROUP_CHATS */

/* Create chat group and return chat id */
db_result_t db_group_chat_create(db_t* db, db_chat_group_t* chat_group, uint64_t* out_chat_group_id);
/* Add chats to group */
db_result_t db_group_chat_add_chats(db_t* db, uint64_t user_uid, uint64_t group_id, int* chat_ids, size_t chat_size);
/* Edit name chat group */
db_result_t db_group_chat_edit_name_by_group_id(db_t* db, uint64_t group_id, uint64_t user_uid, char* name);
/* Update chats in group */
db_result_t db_group_chat_edit_chats_by_group_id(db_t* db, uint64_t group_id, uint64_t user_uid, int* chat_ids, size_t chat_size);
/* Delete group with chats */
db_result_t db_group_chat_delete(db_t* db, uint64_t user_uid, uint64_t group_id);
/* Get list group chats */
db_result_t db_group_chat_get_list(db_t* db, uint64_t user_uid, db_chat_group_LIST_t** out_groups);

#endif