#ifndef REDIS_TEST_LOGGER_H
#define REDIS_TEST_LOGGER_H

void logger_info(const char* fmt, ...);
void logger_warn(const char* fmt, ...);
void logger_error(const char* fmt, ...);

#endif
