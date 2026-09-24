#define _XOPEN_SOURCE 700

#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/stat.h>

static int zip_folder(const char* src, const char* dest)
{
    pid_t pid = fork();

    if (pid == 0)
    {
        execlp("zip", "zip", "-r", "-q", dest, src, NULL);
        exit(1);
    }

    int status;
    waitpid(pid, &status, 0);

    return (WIFEXITED(status) && WEXITSTATUS(status) == 0) ? 0 : -1;
}

static void remove_recursive(const char* path)
{
    DIR* dir = opendir(path);
    if (!dir)
        return;

    struct dirent* entry;

    while ((entry = readdir(dir)) != NULL)
    {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
            continue;

        char full[512];
        snprintf(full, sizeof(full), "%s/%s", path, entry->d_name);

        struct stat st;
        if (stat(full, &st) == -1)
            continue;

        if (S_ISDIR(st.st_mode))
        {
            remove_recursive(full);
        }
        else
        {
            remove(full);
        }
    }

    closedir(dir);
    rmdir(path);
}

void compress_dirs()
{
    DIR* d = opendir(current_log_dir);
    if (!d)
        return;

    struct dirent* entry;

    while ((entry = readdir(d)) != NULL)
    {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        char full_path[512];
        snprintf(full_path, sizeof(full_path), "%s/%s", current_log_dir, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) == -1)
            continue;

        if (!S_ISDIR(st.st_mode))
            continue;

        if (strcmp(full_path, current_log_dir) == 0)
            continue;

        char zip_path[1024];
        snprintf(zip_path, sizeof(zip_path), "%s.zip", full_path);

        if (access(zip_path, F_OK) == 0)
            continue;

        if (zip_folder(full_path, zip_path) == 0)
        {
            remove_recursive(full_path);
        }
    }

    closedir(d);
}