#include "cron/cron.h"
#include "cron/jobs.h"

#include "logger.h"
#include "postgres/postgres.h"

static cron_scheduler_t* scheduler = NULL;

static int database_health_job(void* context)
{
    app_context_t* app = context;
    if (!app || !app->db)
    {
        logger_error("database_health_job: database is not initialized");
        return -1;
    }

    db_result_t result = execute_sql(app->db, "SELECT 1", NULL, 0);
    if (result != DB_OK)
    {
        logger_error("database_health_job: PostgreSQL health check failed: %s", db_error_message(app->db));
        return -1;
    }

    return 0;
}

int cron_init(app_context_t* app)
{
    if (!app)
        return -1;

    if (scheduler)
        return 0;

    cron_scheduler_config_t config = {
        .worker_count = 1,
    };

    scheduler = cron_scheduler_create(&config);
    if (!scheduler)
        return -1;

    cron_job_config_t database_health = {
        .name = "database-health",
        .expression = "0 */5 * * * *",
        .execute = database_health_job,
        .context = app,
        .overlap = CRON_OVERLAP_SKIP,
    };

    if (cron_scheduler_add(scheduler, &database_health) != CRON_OK)
    {
        logger_error("cron_init: failed to register job: %s", cron_scheduler_error(scheduler));
        cron_scheduler_destroy(scheduler);
        scheduler = NULL;
        return -1;
    }

    if (cron_scheduler_start(scheduler) != CRON_OK)
    {
        logger_error("cron_init: failed to start scheduler: %s", cron_scheduler_error(scheduler));

        cron_scheduler_destroy(scheduler);
        scheduler = NULL;
        return -1;
    }

    logger_info("Cron scheduler started");
    return 0;
}

void cron_shutdown(void)
{
    if (!scheduler)
        return;

    cron_scheduler_destroy(scheduler);
    scheduler = NULL;

    logger_info("Cron scheduler stopped");
}
