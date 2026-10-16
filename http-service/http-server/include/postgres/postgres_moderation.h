#ifndef POSTGRES_MODERATION_H
#define POSTGRES_MODERATION_H

#include "postgres/postgres.h"

#define MODERATION_TARGET_AVATAR "avatar"
#define MODERATION_TARGET_CHAT_MEDIA "chat_media"

typedef struct {
    uint64_t target_id;
    uint64_t user_uid;
    char* target_type;
    char* content_ref;
    char* content_type;
} db_moderation_target_t;

typedef struct {
    bool applied;
    uint64_t offense_count;
    bool permanent;
    time_t blocked_until;
} db_moderation_apply_result_t;

/* DB_OK + *out_target == NULL means that the target does not exist anymore
 * or is not a moderatable media target. */
db_result_t db_moderation_target_get(db_t* db,
                                     const char* target_type,
                                     uint64_t target_id,
                                     db_moderation_target_t** out_target);

/* Atomically removes the still-current target, records one unique violation,
 * and creates the escalated account block. Duplicate/redelivered work returns
 * DB_OK with out_result->applied == false. */
db_result_t db_moderation_apply_violation(db_t* db,
                                          const db_moderation_target_t* target,
                                          uint64_t reporter_uid,
                                          db_moderation_apply_result_t* out_result);

void db_moderation_target_free(db_moderation_target_t* target);

#endif
