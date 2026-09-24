#include "utilities.h"

#include <argon2.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

char* hash_password(const char* password)
{
    char* encoded = malloc(ARGON2_HASH_LEN);
    uint8_t salt[16];

    arc4random_buf(salt, sizeof(salt));

    if (argon2id_hash_encoded(3, 65536, 1, password, strlen(password), salt, sizeof(salt), 32, encoded, ARGON2_HASH_LEN) != ARGON2_OK)
    {
        free(encoded);
        return NULL;
    }

    return encoded;
}

int verify_password(const char* password, const char* hash)
{
    if (!password || !hash || !password[0] || !hash[0])
        return 0;

    return argon2id_verify(hash, password, strlen(password)) == ARGON2_OK;
}
