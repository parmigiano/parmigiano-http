#ifndef POSTGRES_MESSAGES_H
#define POSTGRES_MESSAGES_H

#include "postgres.h"

#include <time.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint64_t id;
    time_t created_at;
    time_t updated_at;
    time_t* deleted_at;
    uint64_t chat_id;
    uint64_t sender_uid;
    char* content;
    char* content_type;
    char* attachments;
    bool is_edited;
    bool is_deleted;
    bool is_pinned;
} message_t;

typedef struct {
    uint64_t id;
    uint64_t message_id;
    uint64_t receiver_uid;
    time_t delivered_at;
    time_t* read_at;
} message_status_t;

typedef struct {
    uint64_t id;
    uint64_t message_id;
    char* old_content;
    char* new_content;
    uint64_t* editor_uid;
    time_t edited_at;
} message_edit_t;

typedef struct {
    uint64_t id;
    uint64_t chat_id;
    uint64_t sender_uid;
    char* content;
    char* content_type;
    bool is_edited;
    bool is_pinned;
    time_t delivered_at;
    time_t* read_at;
    char* edit_content;
} ones_message_t;

/* SQLs */
/* Create message */
db_result_t db_message_create(PGconn* conn, message_t* msg, uint64_t* out_message_id);
/* Create message status */
db_result_t db_message_create_status(PGconn* conn, uint64_t message_id, uint64_t chat_id, uint64_t sender_uid);
/* Create message with transaction */
db_result_t db_message_create_all(PGconn* conn, message_t* msg);

#endif