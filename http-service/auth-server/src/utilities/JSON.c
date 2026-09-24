#include "utilities.h"

#include <string.h>

char* escape_json_string(const char* input)
{
    if (!input)
        return strdup("");

    size_t len = strlen(input);

    size_t max_len = len * 2 + 1;
    char* escaped = malloc(max_len);
    if (!escaped)
        return NULL;

    size_t j = 0;
    for (size_t i = 0; i < len; i++)
    {
        char c = input[i];
        switch (c)
        {
        case '"':
            escaped[j++] = '\\';
            escaped[j++] = '"';
            break;
        case '\\':
            escaped[j++] = '\\';
            escaped[j++] = '\\';
            break;
        case '\b':
            escaped[j++] = '\\';
            escaped[j++] = 'b';
            break;
        case '\f':
            escaped[j++] = '\\';
            escaped[j++] = 'f';
            break;
        case '\n':
            escaped[j++] = '\\';
            escaped[j++] = 'n';
            break;
        case '\r':
            escaped[j++] = '\\';
            escaped[j++] = 'r';
            break;
        case '\t':
            escaped[j++] = '\\';
            escaped[j++] = 't';
            break;
        default:
            escaped[j++] = c;
        }
    }

    escaped[j] = '\0';
    return escaped;
}