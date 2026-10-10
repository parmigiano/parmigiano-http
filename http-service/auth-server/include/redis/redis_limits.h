#ifndef REDIS_LIMITS_H
#define REDIS_LIMITS_H

int redis_check_limit_email(const char* email, int max_per_day);

int redis_increment_limit_email(const char* email);

#endif