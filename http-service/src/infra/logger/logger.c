#include "logger.h"

#include <time.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <pthread.h>
#include <sys/stat.h>

#define QUEUE_SIZE 2048

typedef struct
{
    char filename[64];
    char message[2048];
} log_entry;

char current_log_dir[256];

static log_entry queue[QUEUE_SIZE];
static int head = 0, tail = 0;

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

static void ensure_log_dir()
{
    time_t ti = time(NULL);
    struct tm* tm = localtime(&ti);

    char dir[256];
    snprintf(dir, sizeof(dir), "./logs/log_%02d%02d%d", tm->tm_mday, tm->tm_mon + 1, tm->tm_year + 1900);

    if (strcmp(dir, current_log_dir) != 0)
    {
        mkdir("./logs", 0755);
        mkdir(dir, 0755);
        strcpy(current_log_dir, dir);
    }
}

static void* worker(void* arg)
{
    while (1)
    {
        pthread_mutex_lock(&mutex);

        while (head == tail)
        {
            pthread_cond_wait(&cond, &mutex);
        }

        log_entry entry = queue[head];
        head = (head + 1) % QUEUE_SIZE;

        pthread_mutex_unlock(&mutex);

        ensure_log_dir();

        char path[512];
        snprintf(path, sizeof(path), "%s/%s", current_log_dir, entry.filename);

        FILE* f = fopen(path, "a");
        if (f)
        {
            fprintf(f, "%s\n", entry.message);
            fclose(f);
        }
    }

    return NULL;
}

static void log_with_level(const char* level, const char* filename, const char* fmt, va_list args)
{
    time_t t = time(NULL);
    struct tm* tm = localtime(&t);

    char message[1024];
    char formatted[2048];

    vsnprintf(message, sizeof(message), fmt, args);

    snprintf(formatted, sizeof(formatted), "[%02d.%02d.%d %02d:%02d:%02d] [%s] %s", tm->tm_mday, tm->tm_mon + 1, tm->tm_year + 1900, tm->tm_hour,
             tm->tm_min, tm->tm_sec, level, message);

    pthread_mutex_lock(&mutex);

    int next = (tail + 1) % QUEUE_SIZE;
    if (next != head)
    {
        strcpy(queue[tail].filename, filename);
        strcpy(queue[tail].message, formatted);
        tail = next;
        pthread_cond_signal(&cond);
    }
    else
    {
        fprintf(stderr, "logger: queue overflow, dropping log message\n");
    }

    pthread_mutex_unlock(&mutex);
}

void logger_init()
{
    const char* log_env_dir = getenv("LOG_DIR");
    if (!log_env_dir || strlen(log_env_dir) == 0)
    {
        snprintf(current_log_dir, sizeof(current_log_dir), "./logs");
    }
    else
    {
        snprintf(current_log_dir, sizeof(current_log_dir), "%s", log_env_dir);
    }

    pthread_t thread_1;
    pthread_create(&thread_1, NULL, worker, NULL);
}

void logger_info(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    log_with_level("INFO", "info.log", fmt, args);
    va_end(args);
}

void logger_warn(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    log_with_level("WARN", "warning.log", fmt, args);
    va_end(args);
}

void logger_error(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    log_with_level("ERROR", "errors.log", fmt, args);
    va_end(args);
}