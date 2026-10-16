#include "utilities.h"

#include <string.h>

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

char* html_escape(const char* src)
{
    if (!src)
        src = "";

    size_t len = 0;
    for (const char* p = src; *p; ++p)
    {
        if (*p == '&')
            len += 5;
        else if (*p == '<' || *p == '>')
            len += 4;
        else
            len += 1;
    }

    char* out = malloc(len + 1);
    if (!out)
        return NULL;

    char* w = out;
    for (const char* p = src; *p; ++p)
    {
        if (*p == '&')
        {
            memcpy(w, "&amp;", 5);
            w += 5;
        }
        else if (*p == '<')
        {
            memcpy(w, "&lt;", 4);
            w += 4;
        }
        else if (*p == '>')
        {
            memcpy(w, "&gt;", 4);
            w += 4;
        }
        else
        {
            *w++ = *p;
        }
    }

    *w = '\0';
    return out;
}