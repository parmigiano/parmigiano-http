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
bool is_simple_password(const char *password);

/* email.c */
int send_email_async(const char* to, const char* subject, const char* body, const char* cc);
int send_email(const char* to, const char* subject, const char* body, const char* cc);

/* libretranslate.c */
char* translate(const char* text, const char* source, const char* target);
char* detect_lang(const char* text);
char* get_translated_text(const char* json);

/* mime.c */
const char* map_mime_to_msg_type(const char* mime);

/* ai.c */
void start_ai_worker();
void enqueue_ai_request(const char* text, void (*callback)(const char* result, void* arg), void* arg);
void callback_ai_send_response(const char* result, void* arg);

char* get_ollama_response(const char* json);
char* call_ai_text(const char* prompt);

#endif