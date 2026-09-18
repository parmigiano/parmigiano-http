#ifndef MODERATION_H
#define MODERATION_H

#include "postgres/postgres_moderation.h"

#include <stddef.h>

typedef enum {
    MODERATION_MEDIA_CLEAN = 0,
    MODERATION_MEDIA_REJECTED,
    MODERATION_MEDIA_NOT_FOUND,
    MODERATION_MEDIA_RETRY,
    MODERATION_MEDIA_INVALID,
    MODERATION_MEDIA_ERROR
} moderation_media_result_t;

/* Download the target from the configured S3 bucket and scan it.
 * Video/animated media are sampled into several JPEG frames before NSFW scan. */
moderation_media_result_t moderation_media_scan(const moderation_target_t* target,
                                                char* detail,
                                                size_t detail_size);

/* Best-effort object cleanup after the DB target has been atomically removed. */
int moderation_media_delete_object(const moderation_target_t* target);

#endif
