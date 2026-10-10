#ifndef POSTGRES_VALIDATE_H
#define POSTGRES_VALIDATE_H

#include "postgres/postgres.h"

#include <libchttpx/libchttpx.h>

int db_reply_result(chttpx_request_t* req, chttpx_response_t* res, db_result_t result);

#endif
