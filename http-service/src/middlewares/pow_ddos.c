#include "middlewarex.h"

#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/sha.h>
#include <libchttpx/libchttpx.h>

#define POW_DDOS_SECRET "d92376b55175"
#define POW_DDOS_DIFFICULTY 1

static void generate_challenge(char* buffer, size_t len);

static int hash_matches_difficulty(const char* input, int difficulty);

static int pow_ddos_verify(chttpx_request_t* req);

chttpx_middleware_result_t pow_ddos_middleware(chttpx_request_t* req, chttpx_response_t* res)
{
    /* Free routes */
    if (strstr(req->path, "doc.api/swagger") != NULL)
    {
        return next;
    }

    const char* x_debug = cHTTPX_HeaderGet(req, "X-Debug");
    if (x_debug && strcmp(x_debug, "1") == 0)
    {
        return next;
    }

    const char* env_pow_ddos_difficulty = getenv("POW_DDOS_DIFFICULTY");
    int difficulty = POW_DDOS_DIFFICULTY;

    if (env_pow_ddos_difficulty != NULL)
    {
        char* endptr;
        long val = strtol(env_pow_ddos_difficulty, &endptr, 10);
        if (*endptr == '\0')
        {
            difficulty = (int)val;
        }
    }

    if (!pow_ddos_verify(req))
    {
        char challenge[64];
        generate_challenge(challenge, sizeof(challenge));

        *res = cHTTPX_ResJson(cHTTPX_StatusAccepted, "{\"message\": {\"challenge\": \"%s\", \"difficulty\": %d}}", challenge, difficulty);
        return out;
    }

    return next;
}

static void generate_challenge(char* buffer, size_t len)
{
    const char* env_pow_ddos_secret = getenv("POW_DDOS_SECRET");
    if (!env_pow_ddos_secret)
    {
        env_pow_ddos_secret = POW_DDOS_SECRET;
    }

    time_t t = time(NULL) / 5;
    snprintf(buffer, len, "%s:%ld", env_pow_ddos_secret, t);
}

static int hash_matches_difficulty(const char* input, int difficulty)
{
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256((unsigned char*)input, strlen(input), hash);

    for (int i = 0; i < difficulty; ++i)
    {
        int byte_idx = i / 2;
        int half = i % 2;

        unsigned char val = hash[byte_idx];

        if (half == 0)
        {
            if ((val >> 4) != 0)
                return 0;
        }
        else
        {
            if ((val & 0x0F) != 0)
                return 0;
        }
    }

    return 1;
}

static int pow_ddos_verify(chttpx_request_t* req)
{
    const char* challenge = cHTTPX_HeaderGet(req, "Pow-Challenge");
    const char* nonce = cHTTPX_HeaderGet(req, "Pow-Nonce");

    if (!challenge || !nonce)
        return 0;

    const char* env_pow_ddos_difficulty = getenv("POW_DDOS_DIFFICULTY");
    int difficulty = POW_DDOS_DIFFICULTY;

    if (env_pow_ddos_difficulty != NULL)
    {
        char* endptr;
        long val = strtol(env_pow_ddos_difficulty, &endptr, 10);
        if (*endptr == '\0')
        {
            difficulty = (int)val;
        }
    }

    char input[256];
    snprintf(input, sizeof(input), "%s%s", challenge, nonce);

    return hash_matches_difficulty(input, difficulty);
}