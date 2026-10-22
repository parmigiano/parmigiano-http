#define _POSIX_C_SOURCE 200809L

#include "cron/cron.h"

#include "../vendor/ccronexpr/ccronexpr.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct cron_job cron_job_t;

typedef struct cron_task {
    cron_job_t* job;
    struct cron_task* next;
} cron_task_t;

struct cron_job {
    char* name;
    cron_expr expression;
    cron_job_fn execute;
    void* context;
    cron_overlap_t overlap;
    time_t next_run;
    size_t pending_runs;
    cron_job_t* next;
};

struct cron_scheduler {
    pthread_mutex_t mutex;
    pthread_cond_t scheduler_cv;
    pthread_cond_t worker_cv;

    pthread_t scheduler_thread;
    pthread_t* worker_threads;
    size_t worker_count;
    size_t workers_started;

    cron_job_t* jobs;
    cron_task_t* queue_head;
    cron_task_t* queue_tail;

    int started;
    int stopping;
    char error[256];
};

static void set_error_locked(cron_scheduler_t* scheduler, const char* message)
{
    snprintf(scheduler->error, sizeof(scheduler->error), "%s", message ? message : "");
}

static void clear_error_locked(cron_scheduler_t* scheduler)
{
    scheduler->error[0] = '\0';
}

static cron_job_t* find_job_locked(cron_scheduler_t* scheduler, const char* name)
{
    for (cron_job_t* job = scheduler->jobs; job; job = job->next)
    {
        if (strcmp(job->name, name) == 0)
            return job;
    }

    return NULL;
}

static int enqueue_locked(cron_scheduler_t* scheduler, cron_job_t* job)
{
    cron_task_t* task = malloc(sizeof(*task));
    if (!task)
    {
        set_error_locked(scheduler, "out of memory while queuing cron job");
        return 0;
    }

    task->job = job;
    task->next = NULL;

    if (scheduler->queue_tail)
        scheduler->queue_tail->next = task;
    else
        scheduler->queue_head = task;

    scheduler->queue_tail = task;
    job->pending_runs++;
    return 1;
}

static cron_task_t* dequeue_locked(cron_scheduler_t* scheduler)
{
    cron_task_t* task = scheduler->queue_head;
    if (!task)
        return NULL;

    scheduler->queue_head = task->next;
    if (!scheduler->queue_head)
        scheduler->queue_tail = NULL;

    task->next = NULL;
    return task;
}

static void* worker_main(void* arg)
{
    cron_scheduler_t* scheduler = arg;

    for (;;)
    {
        pthread_mutex_lock(&scheduler->mutex);

        while (!scheduler->queue_head && !scheduler->stopping)
            pthread_cond_wait(&scheduler->worker_cv, &scheduler->mutex);

        if (!scheduler->queue_head && scheduler->stopping)
        {
            pthread_mutex_unlock(&scheduler->mutex);
            return NULL;
        }

        cron_task_t* task = dequeue_locked(scheduler);
        pthread_mutex_unlock(&scheduler->mutex);

        if (!task)
            continue;

        cron_job_t* job = task->job;
        free(task);

        job->execute(job->context);

        pthread_mutex_lock(&scheduler->mutex);
        if (job->pending_runs > 0)
            job->pending_runs--;
        pthread_mutex_unlock(&scheduler->mutex);
    }
}

static time_t schedule_due_jobs_locked(cron_scheduler_t* scheduler, time_t now)
{
    time_t earliest = CRON_INVALID_INSTANT;
    int queued = 0;

    for (cron_job_t* job = scheduler->jobs; job; job = job->next)
    {
        if (job->next_run != CRON_INVALID_INSTANT && job->next_run <= now)
        {
            if (job->overlap == CRON_OVERLAP_QUEUE || job->pending_runs == 0)
                queued |= enqueue_locked(scheduler, job);

            job->next_run = cron_next(&job->expression, now);
            if (job->next_run == CRON_INVALID_INSTANT)
                set_error_locked(scheduler, "failed to calculate the next cron run");
        }

        if (job->next_run != CRON_INVALID_INSTANT &&
            (earliest == CRON_INVALID_INSTANT || job->next_run < earliest))
        {
            earliest = job->next_run;
        }
    }

    if (queued)
        pthread_cond_broadcast(&scheduler->worker_cv);

    return earliest;
}

static void* scheduler_main(void* arg)
{
    cron_scheduler_t* scheduler = arg;

    pthread_mutex_lock(&scheduler->mutex);

    while (!scheduler->stopping)
    {
        time_t earliest = schedule_due_jobs_locked(scheduler, time(NULL));

        if (scheduler->stopping)
            break;

        if (earliest == CRON_INVALID_INSTANT)
        {
            pthread_cond_wait(&scheduler->scheduler_cv, &scheduler->mutex);
            continue;
        }

        struct timespec deadline = {
            .tv_sec = earliest,
            .tv_nsec = 0,
        };

        pthread_cond_timedwait(&scheduler->scheduler_cv, &scheduler->mutex, &deadline);
    }

    pthread_mutex_unlock(&scheduler->mutex);
    return NULL;
}

cron_scheduler_t* cron_scheduler_create(const cron_scheduler_config_t* config)
{
    size_t worker_count = config && config->worker_count ? config->worker_count : 1;
    if (worker_count > 1024)
        return NULL;

    cron_scheduler_t* scheduler = calloc(1, sizeof(*scheduler));
    if (!scheduler)
        return NULL;

    scheduler->worker_count = worker_count;
    scheduler->worker_threads = calloc(worker_count, sizeof(*scheduler->worker_threads));
    if (!scheduler->worker_threads)
    {
        free(scheduler);
        return NULL;
    }

    if (pthread_mutex_init(&scheduler->mutex, NULL) != 0)
    {
        free(scheduler->worker_threads);
        free(scheduler);
        return NULL;
    }

    if (pthread_cond_init(&scheduler->scheduler_cv, NULL) != 0)
    {
        pthread_mutex_destroy(&scheduler->mutex);
        free(scheduler->worker_threads);
        free(scheduler);
        return NULL;
    }

    if (pthread_cond_init(&scheduler->worker_cv, NULL) != 0)
    {
        pthread_cond_destroy(&scheduler->scheduler_cv);
        pthread_mutex_destroy(&scheduler->mutex);
        free(scheduler->worker_threads);
        free(scheduler);
        return NULL;
    }

    return scheduler;
}

cron_result_t cron_scheduler_add(cron_scheduler_t* scheduler, const cron_job_config_t* config)
{
    if (!scheduler || !config || !config->name || !config->name[0] ||
        !config->expression || !config->expression[0] || !config->execute ||
        (config->overlap != CRON_OVERLAP_SKIP && config->overlap != CRON_OVERLAP_QUEUE))
    {
        return CRON_INVALID_ARGUMENT;
    }

    if (strlen(config->expression) > 256)
        return CRON_INVALID_EXPRESSION;

    cron_job_t* job = calloc(1, sizeof(*job));
    if (!job)
        return CRON_ERROR;

    job->name = strdup(config->name);
    if (!job->name)
    {
        free(job);
        return CRON_ERROR;
    }

    const char* parse_error = NULL;
    cron_parse_expr(config->expression, &job->expression, &parse_error);
    if (parse_error)
    {
        free(job->name);
        free(job);

        pthread_mutex_lock(&scheduler->mutex);
        set_error_locked(scheduler, parse_error);
        pthread_mutex_unlock(&scheduler->mutex);
        return CRON_INVALID_EXPRESSION;
    }

    job->execute = config->execute;
    job->context = config->context;
    job->overlap = config->overlap;
    job->next_run = CRON_INVALID_INSTANT;

    pthread_mutex_lock(&scheduler->mutex);

    if (scheduler->started)
    {
        set_error_locked(scheduler, "jobs cannot be added while the scheduler is running");
        pthread_mutex_unlock(&scheduler->mutex);
        free(job->name);
        free(job);
        return CRON_INVALID_STATE;
    }

    if (find_job_locked(scheduler, job->name))
    {
        set_error_locked(scheduler, "cron job name already exists");
        pthread_mutex_unlock(&scheduler->mutex);
        free(job->name);
        free(job);
        return CRON_DUPLICATE_JOB;
    }

    clear_error_locked(scheduler);
    job->next = scheduler->jobs;
    scheduler->jobs = job;

    pthread_mutex_unlock(&scheduler->mutex);
    return CRON_OK;
}

static void reset_start_failure(cron_scheduler_t* scheduler, const char* message)
{
    pthread_mutex_lock(&scheduler->mutex);
    scheduler->stopping = 1;
    pthread_cond_broadcast(&scheduler->scheduler_cv);
    pthread_cond_broadcast(&scheduler->worker_cv);
    pthread_mutex_unlock(&scheduler->mutex);

    for (size_t i = 0; i < scheduler->workers_started; ++i)
        pthread_join(scheduler->worker_threads[i], NULL);

    pthread_mutex_lock(&scheduler->mutex);
    scheduler->workers_started = 0;
    scheduler->started = 0;
    scheduler->stopping = 0;
    set_error_locked(scheduler, message);
    pthread_mutex_unlock(&scheduler->mutex);
}

cron_result_t cron_scheduler_start(cron_scheduler_t* scheduler)
{
    if (!scheduler)
        return CRON_INVALID_ARGUMENT;

    pthread_mutex_lock(&scheduler->mutex);

    if (scheduler->started)
    {
        set_error_locked(scheduler, "cron scheduler is already running");
        pthread_mutex_unlock(&scheduler->mutex);
        return CRON_INVALID_STATE;
    }

    if (!scheduler->jobs)
    {
        set_error_locked(scheduler, "cron scheduler has no jobs");
        pthread_mutex_unlock(&scheduler->mutex);
        return CRON_INVALID_STATE;
    }

    time_t now = time(NULL);
    for (cron_job_t* job = scheduler->jobs; job; job = job->next)
    {
        job->next_run = cron_next(&job->expression, now);
        job->pending_runs = 0;
        if (job->next_run == CRON_INVALID_INSTANT)
        {
            set_error_locked(scheduler, "failed to calculate the first cron run");
            pthread_mutex_unlock(&scheduler->mutex);
            return CRON_ERROR;
        }
    }

    clear_error_locked(scheduler);
    scheduler->started = 1;
    scheduler->stopping = 0;
    scheduler->workers_started = 0;
    pthread_mutex_unlock(&scheduler->mutex);

    for (size_t i = 0; i < scheduler->worker_count; ++i)
    {
        if (pthread_create(&scheduler->worker_threads[i], NULL, worker_main, scheduler) != 0)
        {
            reset_start_failure(scheduler, "failed to start cron worker thread");
            return CRON_ERROR;
        }
        scheduler->workers_started++;
    }

    if (pthread_create(&scheduler->scheduler_thread, NULL, scheduler_main, scheduler) != 0)
    {
        reset_start_failure(scheduler, "failed to start cron scheduler thread");
        return CRON_ERROR;
    }

    return CRON_OK;
}

void cron_scheduler_stop(cron_scheduler_t* scheduler)
{
    if (!scheduler)
        return;

    pthread_mutex_lock(&scheduler->mutex);
    if (!scheduler->started)
    {
        pthread_mutex_unlock(&scheduler->mutex);
        return;
    }

    scheduler->stopping = 1;
    pthread_cond_broadcast(&scheduler->scheduler_cv);
    pthread_cond_broadcast(&scheduler->worker_cv);
    pthread_mutex_unlock(&scheduler->mutex);

    pthread_join(scheduler->scheduler_thread, NULL);
    for (size_t i = 0; i < scheduler->workers_started; ++i)
        pthread_join(scheduler->worker_threads[i], NULL);

    pthread_mutex_lock(&scheduler->mutex);
    scheduler->workers_started = 0;
    scheduler->started = 0;
    scheduler->stopping = 0;
    pthread_mutex_unlock(&scheduler->mutex);
}

void cron_scheduler_destroy(cron_scheduler_t* scheduler)
{
    if (!scheduler)
        return;

    cron_scheduler_stop(scheduler);

    cron_task_t* task = scheduler->queue_head;
    while (task)
    {
        cron_task_t* next = task->next;
        free(task);
        task = next;
    }

    cron_job_t* job = scheduler->jobs;
    while (job)
    {
        cron_job_t* next = job->next;
        free(job->name);
        free(job);
        job = next;
    }

    pthread_cond_destroy(&scheduler->worker_cv);
    pthread_cond_destroy(&scheduler->scheduler_cv);
    pthread_mutex_destroy(&scheduler->mutex);
    free(scheduler->worker_threads);
    free(scheduler);
}

const char* cron_scheduler_error(const cron_scheduler_t* scheduler)
{
    return scheduler ? scheduler->error : "cron scheduler is null";
}
