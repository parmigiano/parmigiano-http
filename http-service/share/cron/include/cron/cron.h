#ifndef CRON_CRON_H
#define CRON_CRON_H

#include <stddef.h>

typedef struct cron_scheduler cron_scheduler_t;

typedef int (*cron_job_fn)(void* context);

typedef enum {
    CRON_OK = 0,
    CRON_ERROR = -1,
    CRON_INVALID_ARGUMENT = -2,
    CRON_INVALID_EXPRESSION = -3,
    CRON_DUPLICATE_JOB = -4,
    CRON_INVALID_STATE = -5
} cron_result_t;

typedef enum {
    CRON_OVERLAP_SKIP = 0,
    CRON_OVERLAP_QUEUE = 1
} cron_overlap_t;

typedef struct {
    size_t worker_count;
} cron_scheduler_config_t;

typedef struct {
    /* Job names. */
    const char* name;

    /* second minute hour day month weekday. */
    const char* expression;

    cron_job_fn execute;
    void* context;
    cron_overlap_t overlap;
} cron_job_config_t;

cron_scheduler_t* cron_scheduler_create(const cron_scheduler_config_t* config);
cron_result_t cron_scheduler_add(cron_scheduler_t* scheduler, const cron_job_config_t* job);
cron_result_t cron_scheduler_start(cron_scheduler_t* scheduler);

void cron_scheduler_stop(cron_scheduler_t* scheduler);
void cron_scheduler_destroy(cron_scheduler_t* scheduler);

const char* cron_scheduler_error(const cron_scheduler_t* scheduler);

#endif
