#include "postgres/postgres_moderation.h"

#include "logger.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

static bool moderation_target_type_valid(const char* target_type)
{
    return target_type &&
           (strcmp(target_type, MODERATION_TARGET_AVATAR) == 0 ||
            strcmp(target_type, MODERATION_TARGET_CHAT_MEDIA) == 0);
}

void db_moderation_target_free(moderation_target_t* target)
{
    if (!target)
        return;

    free(target->target_type);
    free(target->content_ref);
    free(target->content_type);
    free(target);
}

db_result_t db_moderation_target_get(PGconn* conn,
                                     const char* target_type,
                                     uint64_t target_id,
                                     moderation_target_t** out_target)
{
    if (!conn || !out_target || !moderation_target_type_valid(target_type) || target_id == 0)
        return DB_ERROR;

    *out_target = NULL;

    const char* query = NULL;
    if (strcmp(target_type, MODERATION_TARGET_AVATAR) == 0)
    {
        query = "SELECT user_uid, avatar, 'image' "
                "FROM user_profiles "
                "WHERE user_uid = $1::bigint "
                "  AND avatar IS NOT NULL "
                "  AND btrim(avatar) <> '' "
                "LIMIT 1";
    }
    else
    {
        query = "SELECT sender_uid, content, content_type "
                "FROM messages "
                "WHERE id = $1::bigint "
                "  AND is_deleted = FALSE "
                "  AND content <> '' "
                "  AND content_type IN ('image', 'video') "
                "LIMIT 1";
    }

    char target_id_str[32];
    snprintf(target_id_str, sizeof(target_id_str), "%" PRIu64, target_id);
    const char* params[1] = {target_id_str};

    PGresult* result = db_exec_params(conn, query, 1, params);
    if (!result)
        return DB_ERROR;

    if (PQresultStatus(result) != PGRES_TUPLES_OK)
    {
        logger_error("db_moderation_target_get target_type={%s} target_id={%" PRIu64 "}: %s",
                     target_type, target_id, PQresultErrorMessage(result));
        PQclear(result);
        return DB_ERROR;
    }

    if (PQntuples(result) == 0)
    {
        PQclear(result);
        return DB_OK;
    }

    if (PQgetisnull(result, 0, 0) || PQgetisnull(result, 0, 1) || PQgetisnull(result, 0, 2))
    {
        PQclear(result);
        return DB_ERROR;
    }

    moderation_target_t* target = calloc(1, sizeof(*target));
    if (!target)
    {
        PQclear(result);
        return DB_ERROR;
    }

    target->target_id = target_id;
    target->user_uid = strtoull(PQgetvalue(result, 0, 0), NULL, 10);
    target->target_type = strdup(target_type);
    target->content_ref = strdup(PQgetvalue(result, 0, 1));
    target->content_type = strdup(PQgetvalue(result, 0, 2));

    PQclear(result);

    if (!target->user_uid || !target->target_type || !target->content_ref || !target->content_type)
    {
        db_moderation_target_free(target);
        return DB_ERROR;
    }

    *out_target = target;
    return DB_OK;
}

static const char* moderation_apply_query(const char* target_type)
{
    static const char* avatar_query =
        "WITH removed AS ("
        "  UPDATE user_profiles "
        "  SET avatar = NULL "
        "  WHERE user_uid = $1::bigint AND avatar = $2 "
        "  RETURNING user_uid"
        "), inserted AS ("
        "  INSERT INTO moderation_violations "
        "      (user_uid, reporter_uid, target_type, target_id, content_fingerprint, content_ref, detector) "
        "  SELECT user_uid, NULLIF($3, '')::bigint, $4, $5::bigint, md5($2), $2, 'nsfw' "
        "  FROM removed "
        "  ON CONFLICT (target_type, target_id, content_fingerprint) DO NOTHING "
        "  RETURNING user_uid"
        "), offense AS ("
        "  SELECT i.user_uid, COUNT(v.id)::bigint AS offense_count "
        "  FROM inserted i "
        "  JOIN moderation_violations v ON v.user_uid = i.user_uid "
        "  GROUP BY i.user_uid"
        "), blocked AS ("
        "  INSERT INTO user_blocks (user_uid, blocked_until, reason, blocked_by_uid, metadata) "
        "  SELECT o.user_uid, "
        "         CASE o.offense_count "
        "           WHEN 1 THEN now() + interval '1 hour' "
        "           WHEN 2 THEN now() + interval '24 hours' "
        "           WHEN 3 THEN now() + interval '7 days' "
        "           WHEN 4 THEN now() + interval '30 days' "
        "           ELSE NULL "
        "         END, "
        "         'automatic moderation: prohibited media', NULL, "
        "         jsonb_build_object('source', 'nsfw', 'target_type', $4, "
        "                            'target_id', $5::bigint, 'offense_count', o.offense_count, "
        "                            'penalty_seconds', CASE o.offense_count "
        "                                WHEN 1 THEN 3600 WHEN 2 THEN 86400 "
        "                                WHEN 3 THEN 604800 WHEN 4 THEN 2592000 ELSE NULL END) "
        "  FROM offense o "
        "  RETURNING user_uid, blocked_until"
        ") "
        "SELECT o.offense_count::text, "
        "       COALESCE(EXTRACT(EPOCH FROM b.blocked_until)::bigint, 0)::text "
        "FROM offense o JOIN blocked b USING (user_uid)";

    static const char* chat_media_query =
        "WITH removed AS ("
        "  UPDATE messages "
        "  SET is_deleted = TRUE, deleted_at = now(), content = '', attachments = NULL "
        "  WHERE id = $5::bigint "
        "    AND sender_uid = $1::bigint "
        "    AND content = $2 "
        "    AND is_deleted = FALSE "
        "  RETURNING sender_uid AS user_uid"
        "), inserted AS ("
        "  INSERT INTO moderation_violations "
        "      (user_uid, reporter_uid, target_type, target_id, content_fingerprint, content_ref, detector) "
        "  SELECT user_uid, NULLIF($3, '')::bigint, $4, $5::bigint, md5($2), $2, 'nsfw' "
        "  FROM removed "
        "  ON CONFLICT (target_type, target_id, content_fingerprint) DO NOTHING "
        "  RETURNING user_uid"
        "), offense AS ("
        "  SELECT i.user_uid, COUNT(v.id)::bigint AS offense_count "
        "  FROM inserted i "
        "  JOIN moderation_violations v ON v.user_uid = i.user_uid "
        "  GROUP BY i.user_uid"
        "), blocked AS ("
        "  INSERT INTO user_blocks (user_uid, blocked_until, reason, blocked_by_uid, metadata) "
        "  SELECT o.user_uid, "
        "         CASE o.offense_count "
        "           WHEN 1 THEN now() + interval '1 hour' "
        "           WHEN 2 THEN now() + interval '24 hours' "
        "           WHEN 3 THEN now() + interval '7 days' "
        "           WHEN 4 THEN now() + interval '30 days' "
        "           ELSE NULL "
        "         END, "
        "         'automatic moderation: prohibited media', NULL, "
        "         jsonb_build_object('source', 'nsfw', 'target_type', $4, "
        "                            'target_id', $5::bigint, 'offense_count', o.offense_count, "
        "                            'penalty_seconds', CASE o.offense_count "
        "                                WHEN 1 THEN 3600 WHEN 2 THEN 86400 "
        "                                WHEN 3 THEN 604800 WHEN 4 THEN 2592000 ELSE NULL END) "
        "  FROM offense o "
        "  RETURNING user_uid, blocked_until"
        ") "
        "SELECT o.offense_count::text, "
        "       COALESCE(EXTRACT(EPOCH FROM b.blocked_until)::bigint, 0)::text "
        "FROM offense o JOIN blocked b USING (user_uid)";

    if (strcmp(target_type, MODERATION_TARGET_AVATAR) == 0)
        return avatar_query;
    if (strcmp(target_type, MODERATION_TARGET_CHAT_MEDIA) == 0)
        return chat_media_query;
    return NULL;
}

db_result_t db_moderation_apply_violation(PGconn* conn,
                                          const moderation_target_t* target,
                                          uint64_t reporter_uid,
                                          moderation_apply_result_t* out_result)
{
    if (!conn || !target || !out_result || !target->content_ref ||
        !moderation_target_type_valid(target->target_type) ||
        !target->user_uid || !target->target_id)
        return DB_ERROR;

    memset(out_result, 0, sizeof(*out_result));

    const char* query = moderation_apply_query(target->target_type);
    if (!query)
        return DB_ERROR;

    char user_uid_str[32];
    char reporter_uid_str[32] = {0};
    char target_id_str[32];

    snprintf(user_uid_str, sizeof(user_uid_str), "%" PRIu64, target->user_uid);
    if (reporter_uid)
        snprintf(reporter_uid_str, sizeof(reporter_uid_str), "%" PRIu64, reporter_uid);
    snprintf(target_id_str, sizeof(target_id_str), "%" PRIu64, target->target_id);

    const char* params[5] = {
        user_uid_str,
        target->content_ref,
        reporter_uid_str,
        target->target_type,
        target_id_str,
    };

    PGresult* result = db_exec_params(conn, query, 5, params);
    if (!result)
        return DB_ERROR;

    if (PQresultStatus(result) != PGRES_TUPLES_OK)
    {
        logger_error("db_moderation_apply_violation target_type={%s} target_id={%" PRIu64 "}: %s",
                     target->target_type, target->target_id, PQresultErrorMessage(result));
        PQclear(result);
        return DB_ERROR;
    }

    if (PQntuples(result) == 0)
    {
        PQclear(result);
        return DB_OK;
    }

    out_result->applied = true;
    out_result->offense_count = strtoull(PQgetvalue(result, 0, 0), NULL, 10);
    out_result->blocked_until = (time_t)strtoll(PQgetvalue(result, 0, 1), NULL, 10);
    out_result->permanent = out_result->blocked_until == 0;

    PQclear(result);
    return DB_OK;
}
