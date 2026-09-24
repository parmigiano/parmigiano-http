#ifndef ENCRYPTION_H
#define ENCRYPTION_H

#include <stddef.h>

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/buffer.h>
#include <openssl/crypto.h>

#define KEY_LEN 32
#define IV_LEN 12
#define TAG_LEN 16

#define HMAC_LEN 32

char *encrypt(const char *plaintext);

char *decrypt(const char *cipher_b64);

#endif