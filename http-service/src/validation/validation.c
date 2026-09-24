#include "utilities.h"

#include <stdio.h>
#include <string.h>

static bool is_valid_utf8(const char* value)
{
    const unsigned char* p = (const unsigned char*)value;

    while (*p)
    {
        if (*p < 0x80)
        {
            p++;
            continue;
        }

        size_t continuation_count;
        unsigned char min_second = 0x80;
        unsigned char max_second = 0xBF;

        if (*p >= 0xC2 && *p <= 0xDF)
            continuation_count = 1;
        else if (*p >= 0xE0 && *p <= 0xEF)
        {
            continuation_count = 2;
            if (*p == 0xE0)
                min_second = 0xA0;
            else if (*p == 0xED)
                max_second = 0x9F;
        }
        else if (*p >= 0xF0 && *p <= 0xF4)
        {
            continuation_count = 3;
            if (*p == 0xF0)
                min_second = 0x90;
            else if (*p == 0xF4)
                max_second = 0x8F;
        }
        else
            return false;

        p++;
        if (*p < min_second || *p > max_second)
            return false;

        for (size_t i = 1; i < continuation_count; ++i)
        {
            p++;
            if (*p < 0x80 || *p > 0xBF)
                return false;
        }

        p++;
    }

    return true;
}

static const char* SimplePasswords[SIMPLE_PASSWORDS_COUNT] = {
    "12345678",  "87654321",    "trustno1",  "starwars",    "batman123", "master12",   "hello123",  "1234567890", "qwerty123", "qwertyui",
    "admin123",  "password",    "password1", "password123", "admin123",  "welcome1",   "letmein1",  "iloveyou",   "sunshine",  "football1",
    "baseball1", "monkey123",   "dragon123", "superman",    "batman123", "trustno1",   "princess1", "charlie1",   "jessica1",  "lovely123",
    "shadow12",  "flower12",    "hello123",  "freedom1",    "mustang1",  "starwars",   "pokemon1",  "123456789a", "zaq12wsx",  "1qaz2wsx",
    "1q2w3e4r",  "1q2w3e4r5t",  "qazwsxed",  "qazwsx12",    "qwertyu1",  "asdfghjk",   "zxcvbnm1",  "myspace1",   "hunter12",  "football12",
    "letmein12", "qwerty12",    "welcome12", "michael1",    "daniel12",  "baseball12", "summer12",  "whatever",   "computer",  "internet",
    "trustme1",  "love1234",    "happy123",  "soccer12",    "cookie12",  "pepper12",   "friends1",  "qwertyuiop", "asdfghjkl", "qwertyui12",
    "hello2024", "welcome2024", "password!", "password@1",  "qwerty!1",  "test1234",   "demo1234",  "user1234",   "root1234",  "qwerty#1",
};

static bool is_simple_password(const char* password)
{
    char lower_pass[255];
    strncpy(lower_pass, password, sizeof(lower_pass));
    lower_pass[sizeof(lower_pass) - 1] = '\0';

    to_lower(lower_pass);

    for (size_t i = 0; i < SIMPLE_PASSWORDS_COUNT; i++)
    {
        if (strcmp(lower_pass, SimplePasswords[i]) == 0)
        {
            return true;
        }
    }

    return false;
}

static bool validation_error(char* error, size_t error_size, const char* key)
{
	if (error && error_size > 0)
		snprintf(error, error_size, "%s", key);

	return false;
}

bool validate_password(const void* value, char* error, size_t error_size)
{
    const char* password = (const char*)value;

    if (!password || !*password)
        return validation_error(error, error_size, "validation.password-empty");

    for (const unsigned char* p = (const unsigned char*)password; *p; p++)
    {
        if (*p < 0x20 || *p == 0x7F)
        {
			return validation_error(error, error_size, "validation.password-control-characters");
		}
    }

    if (is_simple_password(password))
        return validation_error(error, error_size, "error.weak-password");

    return true;
}

bool validate_email(const void* value, char* error, size_t error_size)
{
	const char* email = (const char*)value;

	if (!email || !*email)
		return validation_error(error, error_size, "validation.email-invalid");

	size_t len = strlen(email);

	if (len > 255)
		return validation_error(error, error_size, "validation.email-too-long");

	const char* at = strchr(email, '@');

	if (!at || at == email || at[1] == '\0' || strchr(at + 1, '@'))
		return validation_error(error, error_size, "validation.email-invalid");

	size_t local_len = (size_t)(at - email);
	if (local_len > 64)
		return validation_error(error, error_size, "validation.email-local-too-long");

	if (email[0] == '.' || email[local_len - 1] == '.')
		return validation_error(error, error_size, "validation.email-invalid");

    bool previous_dot = false;

    for (const char* p = email; p < at; p++)
    {
        unsigned char c = (unsigned char)*p;
        if (c == '.')
        {
            if (previous_dot)
                return validation_error(error, error_size, "validation.email-invalid");

            previous_dot = true;
            continue;
        }

        previous_dot = false;

        if (!isalnum(c) && c != '_' && c != '-' && c != '+' && c != '%')
			return validation_error(error, error_size, "validation.email-invalid");
    }

    const char* domain = at + 1;

    if (!strchr(domain, '.'))
        return validation_error(error, error_size, "validation.email-domain-invalid");

    size_t label_len = 0;

    for (const char* p = domain;; p++)
    {
        unsigned char c = (unsigned char)*p;

        if (c == '.' || c == '\0')
        {
            if (label_len == 0)
                return validation_error(error, error_size, "validation.email-domain-invalid");

            if (p[-1] == '-')
                return validation_error(error, error_size, "validation.email-domain-invalid");

            if (c == '\0')
                break;

            label_len = 0;
            continue;
        }

        if (label_len == 0 && c == '-')
            return validation_error(error, error_size, "validation.email-domain-invalid");

        if (!isalnum(c) && c != '-')
            return validation_error(error, error_size, "validation.email-domain-invalid");

        label_len++;

        if (label_len > 63)
            return validation_error(error, error_size, "validation.email-domain-invalid");
    }

    return true;
}

bool validate_username(const void* value, char* error, size_t error_size)
{
    const char* username = (const char*)value;

    if (!username || !*username)
        return validation_error(error, error_size, "validation.username-invalid");

    size_t len = strlen(username);

    if (len < 4 || len > 24)
        return validation_error(error, error_size, "validation.username-length");

    if (!isalnum((unsigned char)username[0]))
        return validation_error(error, error_size, "validation.username-start");

    for (size_t i = 0; i < len; i++)
    {
        unsigned char c = (unsigned char)username[i];

        if (!isalnum(c) && c != '_')
        {
            return validation_error(error, error_size, "validation.username-characters");
        }
    }

    /*
     * Incorrect usernames:
     * ____user
     * user____
     */
    if (username[len - 1] == '_')
        return validation_error(error, error_size, "validation.username-end-underscore");

    if (strstr(username, "__"))
        return validation_error(error, error_size, "validation.username-consecutive-underscores");

    return true;
}

bool validate_name(const void* value, char* error, size_t error_size)
{
    const char* name = (const char*)value;

    if (!name || !*name)
        return validation_error(error, error_size, "validation.name-empty");

    if (!is_valid_utf8(name))
        return validation_error(error, error_size, "validation.name-invalid-utf8");

    const unsigned char* p = (const unsigned char*)name;

    while (*p)
    {
        if (*p < 0x20 || *p == 0x7F)
            return validation_error(error, error_size, "validation.name-invalid-characters");

        p++;
    }

    return true;
}
