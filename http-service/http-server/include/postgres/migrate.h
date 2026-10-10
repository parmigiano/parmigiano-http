#ifndef POSTGRES_MIGRATE_H
#define POSTGRES_MIGRATE_H

#include "postgres/postgres.h"

void run_migrations(db_t* db);

#endif
