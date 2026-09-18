#ifndef LOGGER_H
#define LOGGER_H

#include <libchttpx/libchttpx.h>

extern char current_log_dir[256];

void logger_init();

void logger_info(const char *fmt, ...);
void logger_warn(const char *fmt, ...);
void logger_error(const char *fmt, ...);

void logger_httpx(chttpx_log_level_t level, const char* request_id, const char* message, void* user_data);

void compress_dirs();

#endif
