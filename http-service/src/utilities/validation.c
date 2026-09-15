#include "utilities.h"

#include <string.h>

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

bool is_simple_password(const char* password)
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