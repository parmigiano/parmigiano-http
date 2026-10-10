#ifndef DB_INTERNAL_H
#define DB_INTERNAL_H

#include "postgres/postgres.h"

db_result_t db_checkout(db_t* db, PGconn** out_conn, int* out_slot, int* out_owned);
void db_checkin(db_t* db, int slot, int owned, int discard);
void db_set_last(db_result_t result, const char* message);

#endif
