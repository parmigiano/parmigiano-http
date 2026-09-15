#ifndef MIDDLEWAREX_H
#define MIDDLEWAREX_H

#include <libchttpx/libchttpx.h>

#define LANGUAGE_BASE "en"

chttpx_middleware_result_t x_request_id_middleware(chttpx_request_t* req, chttpx_response_t* res);

chttpx_middleware_result_t language_middleware(chttpx_request_t *req, chttpx_response_t *res);

chttpx_middleware_result_t geoip_block_middleware(chttpx_request_t *req, chttpx_response_t *res);

chttpx_middleware_result_t pow_ddos_middleware(chttpx_request_t* req, chttpx_response_t* res);

chttpx_middleware_result_t authenticate_middleware(chttpx_request_t *req, chttpx_response_t *res);

chttpx_middleware_result_t email_confirmed_middleware(chttpx_request_t *req, chttpx_response_t *res);

#endif