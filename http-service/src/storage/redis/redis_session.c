#include "redis/redis.h"

#include "encryption.h"
#include "redis/redis_session.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cjson/cJSON.h>

static char* session_key(const char* key)
{
    char* buffer = malloc(strlen(key) + 9);
    if (!buffer)
        return NULL;

    sprintf(buffer, "session:%s", key);
    return buffer;
}

static char* session_to_json(const session_t* sess)
{
    cJSON* root = cJSON_CreateObject();
    if (!root)
        return NULL;

    cJSON_AddNumberToObject(root, "user_uid", sess->user_uid);
    cJSON_AddNumberToObject(root, "expires_at", sess->expires_at);

    char* json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json;
}

static int json_to_session(const char* json, session_t* sess)
{
    cJSON* root = cJSON_Parse(json);
    if (!root)
        return 0;

    cJSON* user_uid_item = cJSON_GetObjectItem(root, "user_uid");
    cJSON* expires_at_item = cJSON_GetObjectItem(root, "expires_at");

    if (!cJSON_IsNumber(user_uid_item) || !cJSON_IsNumber(expires_at_item))
    {
        cJSON_Delete(root);
        return 0;
    }

    sess->user_uid = (uint64_t)user_uid_item->valuedouble;
    sess->expires_at = (uint64_t)expires_at_item->valuedouble;

    cJSON_Delete(root);
    return 1;
}

char* redis_session_create(const session_t* sess)
{
    char* json = session_to_json(sess);
    if (!json)
        return NULL;

    char* key = NULL;
    char* session_id = NULL;

    session_id = encrypt(json);
    if (!session_id)
        goto error;

    key = session_key(session_id);
    if (!key)
        goto error;

    redisReply* r = redis_command("EXISTS %s", key);
    if (!r)
        goto error;

    if (r->integer == 1)
    {
        freeReplyObject(r);
        
        r = redis_command("EXPIRE %s %d", key, REDIS_SESSION_TTL);
        if (r)
            freeReplyObject(r);

        free(json);
        free(key);
        return session_id;
    }

    freeReplyObject(r);

    r = redis_command("SET %s %s EX %d", key, json, REDIS_SESSION_TTL);
    if (!r)
        goto error;

    freeReplyObject(r);
    free(json);
    free(key);

    return session_id;

error:
    free(json);
    free(key);
    free(session_id);
    return NULL;
}

session_t* redis_session_get(const char* session_id)
{
    char* key = session_key(session_id);
    if (!key)
        return NULL;

    redisReply* r = redis_command("GET %s", key);
    free(key);

    if (!r || r->type == REDIS_REPLY_NIL)
    {
        if (r)
            freeReplyObject(r);
        return NULL;
    }

    session_t* sess = malloc(sizeof(session_t));
    if (!sess)
    {
        freeReplyObject(r);
        return NULL;
    }

    if (!json_to_session(r->str, sess))
    {
        free(sess);
        sess = NULL;
    }

    freeReplyObject(r);
    return sess;
}

int redis_session_refresh(const char* session_id)
{
    char* key = session_key(session_id);
    if (!key)
        return 0;

    redisReply* r = redis_command("EXPIRE %s %d", key, REDIS_SESSION_TTL);
    free(key);

    int ok = r && r->integer == 1;
    if (r)
        freeReplyObject(r);
    return ok;
}

int redis_session_delete(const char* session_id)
{
    char* key = session_key(session_id);
    if (!key)
        return 0;

    redisReply* r = redis_command("DEL %s", key);
    free(key);

    /* An already expired/revoked key also means the session is gone. */
    int ok = r && r->type == REDIS_REPLY_INTEGER && r->integer >= 0;
    if (r)
        freeReplyObject(r);
    return ok;
}
