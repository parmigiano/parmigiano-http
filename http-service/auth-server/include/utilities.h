#ifndef UTILITIES_H
#define UTILITIES_H

#include <ctype.h>
#include <stdlib.h>
#include <stdbool.h>

#define ARGON2_HASH_LEN 256
#define SIMPLE_PASSWORDS_COUNT 80

/* CURL.c */
struct Memory {
    char* response;
    size_t size;
};

size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp);

/* JSON.c */
char* escape_json_string(const char* input);

/* string.c */
void trim_space(char *str);
bool is_valid(const char *str);
void to_lower(char *str);

/* password.c */
char* hash_password(const char *password);
int verify_password(const char *password, const char *hash);

/* env.c */
int env_init(const char* filename);

/* validation.c */
bool validate_password(const void* value, char* error, size_t error_size);
bool validate_email(const void* value, char* error, size_t error_size);
bool validate_username(const void* value, char* error, size_t error_size);
bool validate_name(const void* value, char* error, size_t error_size);

/* email.c */
int send_email_async(const char* to, const char* subject, const char* body, const char* cc);
int send_email(const char* to, const char* subject, const char* body, const char* cc);

#endif
