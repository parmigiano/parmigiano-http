#include "utilities.h"

void trim_space(char* str)
{
    char *src = str, *dst = str;

    while (*src != '\0')
    {
        if (!isspace((unsigned char)*src))
        {
            *dst = *src;
            dst++;
        }

        src++;
    }

    *dst = '\0';
}

void to_lower(char* str)
{
    while (*str)
    {
        *str = tolower((unsigned char)*str);
        str++;
    }
}

bool is_valid(const char* str)
{
    while (*str)
    {
        if (!isalnum((unsigned char)*str) && *str != '_')
        {
            return false;
        }

        str++;
    }

    return true;
}