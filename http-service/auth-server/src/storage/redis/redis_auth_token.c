#include "redis/redis_auth_token.h"
#include "redis/redis.h"
#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <openssl/err.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#define AUTH_TOKEN_RAW_LEN 32
#define AUTH_TOKEN_TEXT_LEN 43

static const char B64URL[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

static const char TOKEN_CONSUME_LUA[] = "local v = redis.call('GET', KEYS[1]) "
                                        "local other = redis.call('GET', KEYS[2]) "
                                        "if not v then if other then return 2 else return 0 end end "
                                        "local nl = string.find(v, '\\n', 1, true) "
                                        "if not nl then return 4 end "
                                        "local exp = tonumber(string.sub(v, 1, nl - 1)) "
                                        "local email = string.sub(v, nl + 1) "
                                        "if email ~= ARGV[1] then return 2 end "
                                        "local now = tonumber(ARGV[2]) "
                                        "if (not exp) or (not now) or (exp <= now) then redis.call('DEL', KEYS[1]) return 3 end "
                                        "redis.call('DEL', KEYS[1]) "
                                        "return 1";

static int b64url_value(char c)
{
    if (c >= 'A' && c <= 'Z')
        return c - 'A';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 26;
    if (c >= '0' && c <= '9')
        return c - '0' + 52;
    if (c == '-')
        return 62;
    if (c == '_')
        return 63;
    return -1;
}

static void b64url_encode_32(const unsigned char in[AUTH_TOKEN_RAW_LEN], char out[AUTH_TOKEN_TEXT_LEN + 1])
{
    size_t o = 0;

    for (size_t i = 0; i < 30; i += 3)
    {
        unsigned int n = ((unsigned int)in[i] << 16) | ((unsigned int)in[i + 1] << 8) | in[i + 2];
        out[o++] = B64URL[(n >> 18) & 63];
        out[o++] = B64URL[(n >> 12) & 63];
        out[o++] = B64URL[(n >> 6) & 63];
        out[o++] = B64URL[n & 63];
    }

    unsigned int tail = ((unsigned int)in[30] << 16) | ((unsigned int)in[31] << 8);
    out[o++] = B64URL[(tail >> 18) & 63];
    out[o++] = B64URL[(tail >> 12) & 63];
    out[o++] = B64URL[(tail >> 6) & 63];
    out[o] = '\0';
}

static int b64url_decode_32(const char* in, unsigned char out[AUTH_TOKEN_RAW_LEN])
{
    size_t len = strlen(in);
    if (len == 44 && in[43] == '=')
        len = AUTH_TOKEN_TEXT_LEN;
    if (len != AUTH_TOKEN_TEXT_LEN)
        return -1;

    int vals[AUTH_TOKEN_TEXT_LEN];
    for (size_t i = 0; i < AUTH_TOKEN_TEXT_LEN; i++)
    {
        vals[i] = b64url_value(in[i]);
        if (vals[i] < 0)
            return -1;
    }

    if (vals[42] & 0x3)
        return -1;

    size_t o = 0;
    for (size_t i = 0; i < 40; i += 4)
    {
        unsigned int n =
            ((unsigned int)vals[i] << 18) | ((unsigned int)vals[i + 1] << 12) | ((unsigned int)vals[i + 2] << 6) | (unsigned int)vals[i + 3];
        out[o++] = (unsigned char)((n >> 16) & 0xff);
        out[o++] = (unsigned char)((n >> 8) & 0xff);
        out[o++] = (unsigned char)(n & 0xff);
    }

    unsigned int tail = ((unsigned int)vals[40] << 18) | ((unsigned int)vals[41] << 12) | ((unsigned int)vals[42] << 6);
    out[o++] = (unsigned char)((tail >> 16) & 0xff);
    out[o++] = (unsigned char)((tail >> 8) & 0xff);
    return o == AUTH_TOKEN_RAW_LEN ? 0 : -1;
}

static void sha256_hex(const unsigned char raw[AUTH_TOKEN_RAW_LEN], char hex[SHA256_DIGEST_LENGTH * 2 + 1])
{
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(raw, AUTH_TOKEN_RAW_LEN, hash);

    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++)
        sprintf(hex + (i * 2), "%02x", hash[i]);

    hex[SHA256_DIGEST_LENGTH * 2] = '\0';
    memset(hash, 0, sizeof(hash));
}

static const char* token_prefix(redis_auth_token_kind_t kind)
{
    if (kind == REDIS_AUTH_TOKEN_REGISTRATION)
        return "registration_token:";
    if (kind == REDIS_AUTH_TOKEN_LOGIN)
        return "login_token:";

    return NULL;
}

static const char* other_token_prefix(redis_auth_token_kind_t kind)
{
    if (kind == REDIS_AUTH_TOKEN_REGISTRATION)
        return "login_token:";
    if (kind == REDIS_AUTH_TOKEN_LOGIN)
        return "registration_token:";

    return NULL;
}

static redis_auth_token_status_t token_keys(redis_auth_token_kind_t kind, const char* token, char* key, size_t key_len, char* other_key,
                                            size_t other_key_len)
{
    if (!token || token[0] == '\0')
        return REDIS_AUTH_TOKEN_MISSING;

    const char* prefix = token_prefix(kind);
    const char* other = other_token_prefix(kind);
    if (!prefix || !other)
        return REDIS_AUTH_TOKEN_ERROR;

    unsigned char raw[AUTH_TOKEN_RAW_LEN];
    if (b64url_decode_32(token, raw) != 0)
        return REDIS_AUTH_TOKEN_MISMATCH;

    char hex[SHA256_DIGEST_LENGTH * 2 + 1];
    sha256_hex(raw, hex);
    memset(raw, 0, sizeof(raw));

    if (snprintf(key, key_len, "%s%s", prefix, hex) >= (int)key_len || snprintf(other_key, other_key_len, "%s%s", other, hex) >= (int)other_key_len)
        return REDIS_AUTH_TOKEN_ERROR;

    return REDIS_AUTH_TOKEN_OK;
}

static int split_token_value(const char* value, long long* exp_out, const char** email_out)
{
    char* end = NULL;
    long long exp = strtoll(value, &end, 10);
    if (!end || *end != '\n' || exp <= 0)
        return 0;

    *exp_out = exp;
    *email_out = end + 1;
    return (*email_out)[0] != '\0';
}

static redisReply* redis_get(const char* key)
{
    const char* argv[] = {"GET", key};
    size_t argvlen[] = {3, strlen(key)};
    return redis_command_argv(2, argv, argvlen);
}

static void redis_del(const char* key)
{
    const char* argv[] = {"DEL", key};
    size_t argvlen[] = {3, strlen(key)};
    redisReply* reply = redis_command_argv(2, argv, argvlen);
    if (reply)
        freeReplyObject(reply);
}

static redis_auth_token_status_t status_from_value(const char* value, const char* email, const char* key, int delete_expired)
{
    long long exp = 0;
    const char* stored_email = NULL;
    if (!split_token_value(value, &exp, &stored_email))
        return REDIS_AUTH_TOKEN_ERROR;

    if (strcmp(stored_email, email) != 0)
        return REDIS_AUTH_TOKEN_MISMATCH;

    if (exp <= (long long)time(NULL))
    {
        if (delete_expired)
            redis_del(key);
        return REDIS_AUTH_TOKEN_EXPIRED;
    }

    return REDIS_AUTH_TOKEN_OK;
}

redis_auth_token_status_t redis_auth_token_create(redis_auth_token_kind_t kind, const char* email, char* out_token, size_t out_len)
{
    if (!email || email[0] == '\0' || strchr(email, '\n') || !out_token || out_len < AUTH_TOKEN_TEXT_LEN + 1)
        return REDIS_AUTH_TOKEN_ERROR;

    const char* prefix = token_prefix(kind);
    if (!prefix)
        return REDIS_AUTH_TOKEN_ERROR;

    unsigned char raw[AUTH_TOKEN_RAW_LEN];
    if (RAND_bytes(raw, sizeof(raw)) != 1)
    {
        char err_buf[256];
        ERR_error_string_n(ERR_get_error(), err_buf, sizeof(err_buf));
        logger_error("redis_auth_token_create: failed to generate token: %s", err_buf);
        return REDIS_AUTH_TOKEN_ERROR;
    }

    char token[AUTH_TOKEN_TEXT_LEN + 1];
    b64url_encode_32(raw, token);

    char hex[SHA256_DIGEST_LENGTH * 2 + 1];
    sha256_hex(raw, hex);
    memset(raw, 0, sizeof(raw));

    char key[128];
    if (snprintf(key, sizeof(key), "%s%s", prefix, hex) >= (int)sizeof(key))
        return REDIS_AUTH_TOKEN_ERROR;

    char value[320];
    long long exp = (long long)time(NULL) + REDIS_AUTH_TOKEN_TTL;
    if (snprintf(value, sizeof(value), "%lld\n%s", exp, email) >= (int)sizeof(value))
        return REDIS_AUTH_TOKEN_ERROR;

    char ttl[16];
    snprintf(ttl, sizeof(ttl), "%d", REDIS_AUTH_TOKEN_TTL);

    const char* argv[] = {"SET", key, value, "EX", ttl};
    size_t argvlen[] = {3, strlen(key), strlen(value), 2, strlen(ttl)};
    redisReply* reply = redis_command_argv(5, argv, argvlen);
    if (!reply || reply->type == REDIS_REPLY_ERROR)
    {
        logger_error("redis_auth_token_create: redis SET failed: %s", (reply && reply->str) ? reply->str : "no reply");
        if (reply)
            freeReplyObject(reply);
        return REDIS_AUTH_TOKEN_ERROR;
    }

    freeReplyObject(reply);
    memcpy(out_token, token, AUTH_TOKEN_TEXT_LEN + 1);
    return REDIS_AUTH_TOKEN_OK;
}

redis_auth_token_status_t redis_auth_token_check(redis_auth_token_kind_t kind, const char* token, const char* email)
{
    if (!email || email[0] == '\0')
        return REDIS_AUTH_TOKEN_ERROR;

    char key[128];
    char other_key[128];

    redis_auth_token_status_t keys_status = token_keys(kind, token, key, sizeof(key), other_key, sizeof(other_key));
    if (keys_status != REDIS_AUTH_TOKEN_OK)
        return keys_status;

    redisReply* reply = redis_get(key);
    if (!reply || reply->type == REDIS_REPLY_ERROR)
    {
        if (reply)
            freeReplyObject(reply);
        return REDIS_AUTH_TOKEN_ERROR;
    }

    if (reply->type == REDIS_REPLY_NIL)
    {
        freeReplyObject(reply);

        redisReply* other = redis_get(other_key);
        if (!other || other->type == REDIS_REPLY_ERROR)
        {
            if (other)
                freeReplyObject(other);

            return REDIS_AUTH_TOKEN_ERROR;
        }

        int other_exists = other->type == REDIS_REPLY_STRING;

        freeReplyObject(other);

        return other_exists ? REDIS_AUTH_TOKEN_MISMATCH : REDIS_AUTH_TOKEN_EXPIRED;
    }

    if (reply->type != REDIS_REPLY_STRING || !reply->str)
    {
        freeReplyObject(reply);
        return REDIS_AUTH_TOKEN_ERROR;
    }

    redis_auth_token_status_t status = status_from_value(reply->str, email, key, 1);

    freeReplyObject(reply);

    return status;
}

redis_auth_token_status_t redis_auth_token_consume(redis_auth_token_kind_t kind, const char* token, const char* email)
{
    if (!email || email[0] == '\0')
        return REDIS_AUTH_TOKEN_ERROR;

    char key[128];
    char other_key[128];
    redis_auth_token_status_t keys_status = token_keys(kind, token, key, sizeof(key), other_key, sizeof(other_key));
    if (keys_status != REDIS_AUTH_TOKEN_OK)
        return keys_status;

    char now_buf[32];
    snprintf(now_buf, sizeof(now_buf), "%lld", (long long)time(NULL));

    const char* argv[] = {"EVAL", TOKEN_CONSUME_LUA, "2", key, other_key, email, now_buf};
    size_t argvlen[] = {4, sizeof(TOKEN_CONSUME_LUA) - 1, 1, strlen(key), strlen(other_key), strlen(email), strlen(now_buf)};

    redisReply* reply = redis_command_argv(7, argv, argvlen);
    if (!reply || reply->type != REDIS_REPLY_INTEGER)
    {
        logger_error("redis_auth_token_consume: redis EVAL failed: %s", (reply && reply->str) ? reply->str : "no reply");
        if (reply)
            freeReplyObject(reply);
        return REDIS_AUTH_TOKEN_ERROR;
    }

    long long code = reply->integer;
    freeReplyObject(reply);

    if (code == 1)
        return REDIS_AUTH_TOKEN_OK;
    if (code == 2)
        return REDIS_AUTH_TOKEN_MISMATCH;
    if (code == 3)
        return REDIS_AUTH_TOKEN_EXPIRED;
    if (code == 0)
        return REDIS_AUTH_TOKEN_EXPIRED;

    return REDIS_AUTH_TOKEN_ERROR;
}
