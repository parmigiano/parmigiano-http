#ifndef LOGGER_H
#define LOGGER_H

extern char current_log_dir[256];

void logger_init();

void logger_info(const char *fmt, ...);
void logger_warn(const char *fmt, ...);
void logger_error(const char *fmt, ...);

void compress_dirs();

#endif