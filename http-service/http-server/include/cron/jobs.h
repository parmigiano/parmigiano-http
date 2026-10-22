#ifndef CRON_JOBS_H
#define CRON_JOBS_H

#include "httpx.h"

int cron_init(app_context_t* app);

void cron_shutdown(void);

#endif
