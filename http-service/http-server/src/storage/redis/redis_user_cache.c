#include "redis/redis.h"

#include "postgres/postgres_users.h"
#include "redis/redis_user_cache.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <cjson/cJSON.h>

static char* cache_key(const char* prefix, uint64_t user_uid)
{
    size_t len = strlen(prefix) + 32;
    char* buffer = malloc(len);
    if (!buffer)
        return NULL;

    snprintf(buffer, len, "%s%" PRIu64, prefix, user_uid);
    return buffer;
}

static cJSON* add_opt_string(cJSON* root, const char* name, const char* value)
{
    if (!value)
        return cJSON_AddNullToObject(root, name);

    return cJSON_AddStringToObject(root, name, value);
}

static int read_u64(cJSON* root, const char* name, uint64_t* out)
{
    cJSON* item = cJSON_GetObjectItem(root, name);
    if (!cJSON_IsNumber(item) || item->valuedouble < 0)
        return 0;

    *out = (uint64_t)item->valuedouble;
    return 1;
}

static int read_time(cJSON* root, const char* name, time_t* out)
{
    cJSON* item = cJSON_GetObjectItem(root, name);
    if (!cJSON_IsNumber(item))
        return 0;

    *out = (time_t)item->valuedouble;
    return 1;
}

static int read_bool(cJSON* root, const char* name, bool* out)
{
    cJSON* item = cJSON_GetObjectItem(root, name);
    if (!cJSON_IsBool(item))
        return 0;

    *out = cJSON_IsTrue(item);
    return 1;
}

static int read_opt_string(cJSON* root, const char* name, char** out)
{
    cJSON* item = cJSON_GetObjectItem(root, name);
    if (!item)
        return 0;

    if (cJSON_IsNull(item))
    {
        *out = NULL;
        return 1;
    }

    if (!cJSON_IsString(item) || !item->valuestring)
        return 0;

    *out = strdup(item->valuestring);
    return *out != NULL;
}

static char* user_to_json(const db_user_info_t* user)
{
    cJSON* root = cJSON_CreateObject();
    if (!root)
        return NULL;

    if (!cJSON_AddNumberToObject(root, "id", (double)user->id) || !cJSON_AddNumberToObject(root, "created_at", (double)user->created_at) ||
        !cJSON_AddNumberToObject(root, "user_uid", (double)user->user_uid) || !add_opt_string(root, "avatar", user->avatar) ||
        !add_opt_string(root, "name", user->name) || !add_opt_string(root, "username", user->username) ||
        !cJSON_AddBoolToObject(root, "username_visible", user->username_visible) || !add_opt_string(root, "email", user->email) ||
        !cJSON_AddBoolToObject(root, "email_visible", user->email_visible) || !cJSON_AddBoolToObject(root, "email_confirm", user->email_confirm) ||
        !add_opt_string(root, "phone", user->phone) || !cJSON_AddBoolToObject(root, "phone_visible", user->phone_visible) ||
        !add_opt_string(root, "overview", user->overview))
    {
        cJSON_Delete(root);
        return NULL;
    }

    char* json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json;
}

static int json_to_user(const char* json, db_user_info_t* user)
{
    cJSON* root = cJSON_Parse(json);
    if (!root)
        return 0;

    int ok = read_u64(root, "id", &user->id) && read_time(root, "created_at", &user->created_at) && read_u64(root, "user_uid", &user->user_uid) &&
             read_opt_string(root, "avatar", &user->avatar) && read_opt_string(root, "name", &user->name) &&
             read_opt_string(root, "username", &user->username) && read_bool(root, "username_visible", &user->username_visible) &&
             read_opt_string(root, "email", &user->email) && read_bool(root, "email_visible", &user->email_visible) &&
             read_bool(root, "email_confirm", &user->email_confirm) && read_opt_string(root, "phone", &user->phone) &&
             read_bool(root, "phone_visible", &user->phone_visible) && read_opt_string(root, "overview", &user->overview);

    cJSON_Delete(root);
    return ok;
}

int redis_user_info_get(uint64_t user_uid, db_user_info_t** out)
{
    if (!out)
        return -1;

    *out = NULL;

    char* key = cache_key("user:", user_uid);
    if (!key)
        return -1;

    redisReply* reply = redis_command("GET %s", key);
    free(key);

    if (!reply)
        return -1;

    if (reply->type == REDIS_REPLY_NIL)
    {
        freeReplyObject(reply);
        return 0;
    }

    if (reply->type != REDIS_REPLY_STRING || !reply->str)
    {
        freeReplyObject(reply);
        return -1;
    }

    db_user_info_t* user = calloc(1, sizeof(*user));
    if (!user)
    {
        freeReplyObject(reply);
        return -1;
    }

    int parsed = json_to_user(reply->str, user);
    freeReplyObject(reply);

    if (!parsed || user->user_uid != user_uid)
    {
        db_user_info_free(user);
        return 0;
    }

    *out = user;
    return 1;
}

int redis_user_info_set(const db_user_info_t* user)
{
    if (!user)
        return -1;

    char* json = user_to_json(user);
    if (!json)
        return -1;

    char* key = cache_key("user:", user->user_uid);
    if (!key)
    {
        free(json);
        return -1;
    }

    redisReply* reply = redis_command("SET %s %s EX %d", key, json, REDIS_USER_INFO_TTL);
    free(json);
    free(key);

    int ok = reply && reply->type != REDIS_REPLY_ERROR;
    if (reply)
        freeReplyObject(reply);

    return ok ? 0 : -1;
}

int redis_user_info_delete(uint64_t user_uid)
{
    char* key = cache_key("user:", user_uid);
    if (!key)
        return -1;

    redisReply* reply = redis_command("DEL %s", key);
    free(key);

    int ok = reply && reply->type == REDIS_REPLY_INTEGER;
    if (reply)
        freeReplyObject(reply);

    return ok ? 0 : -1;
}

static int block_cache_ttl(const db_user_block_t* block)
{
    if (!block || block->permanent || block->blocked_until <= 0)
        return REDIS_USER_BLOCK_TTL;

    time_t left = block->blocked_until - time(NULL);
    if (left < 1)
        return 1;
    if (left > REDIS_USER_BLOCK_TTL)
        return REDIS_USER_BLOCK_TTL;

    return (int)left;
}

static void block_cache_value(const db_user_block_t* block, char* value, size_t len)
{
    if (!block || (!block->permanent && block->blocked_until > 0 && block->blocked_until <= time(NULL)))
    {
        snprintf(value, len, "0");
        return;
    }

    if (block->permanent || block->blocked_until <= 0)
    {
        snprintf(value, len, "1");
        return;
    }

    snprintf(value, len, "1:%lld", (long long)block->blocked_until);
}

static int parse_block_value(const char* value, int* out_blocked)
{
    if (strcmp(value, "0") == 0)
    {
        *out_blocked = 0;
        return 1;
    }

    if (strcmp(value, "1") == 0)
    {
        *out_blocked = 1;
        return 1;
    }

    if (strncmp(value, "1:", 2) != 0)
        return 0;

    char* end = NULL;
    long long until = strtoll(value + 2, &end, 10);
    if (!end || *end || until <= 0)
        return 0;

    *out_blocked = until > (long long)time(NULL);
    return 1;
}

int redis_user_block_get(uint64_t user_uid, int* out_blocked)
{
    if (!out_blocked)
        return -1;

    char* key = cache_key("user_block:", user_uid);
    if (!key)
        return -1;

    redisReply* reply = redis_command("GET %s", key);
    free(key);

    if (!reply)
        return -1;

    if (reply->type == REDIS_REPLY_NIL)
    {
        freeReplyObject(reply);
        return 0;
    }

    if (reply->type != REDIS_REPLY_STRING || !reply->str)
    {
        freeReplyObject(reply);
        return -1;
    }

    int parsed = parse_block_value(reply->str, out_blocked);
    freeReplyObject(reply);
    return parsed ? 1 : 0;
}

int redis_user_block_set(uint64_t user_uid, const db_user_block_t* block)
{
    char* key = cache_key("user_block:", user_uid);
    if (!key)
        return -1;

    char value[64];
    block_cache_value(block, value, sizeof(value));

    redisReply* reply = redis_command("SET %s %s EX %d", key, value, block_cache_ttl(block));
    free(key);

    int ok = reply && reply->type != REDIS_REPLY_ERROR;
    if (reply)
        freeReplyObject(reply);

    return ok ? 0 : -1;
}

int redis_user_block_delete(uint64_t user_uid)
{
    char* key = cache_key("user_block:", user_uid);
    if (!key)
        return -1;

    redisReply* reply = redis_command("DEL %s", key);
    free(key);

    int ok = reply && reply->type == REDIS_REPLY_INTEGER;
    if (reply)
        freeReplyObject(reply);

    return ok ? 0 : -1;
}
