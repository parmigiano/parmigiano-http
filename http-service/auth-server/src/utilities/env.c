#include "utilities.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int env_init(const char* filename)
{
    FILE* f = fopen(filename, "r");
    if (!f)
        return -1;

    char line[1024];

    while (fgets(line, sizeof(line), f))
    {
        if (line[0] == '#' || line[0] == '\n')
            continue;

        char* eq = strchr(line, '=');
        if (!eq)
            continue;

        *eq = '\0';
        char* key = line;
        char* value = eq + 1;

        value[strcspn(value, "\r\n")] = 0;
        setenv(key, value, 1);
    }

    fclose(f);
    return 0;
}