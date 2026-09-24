#ifndef REDIS_LIMITS_H
#define REDIS_LIMITS_H

int redis_check_limit_email_and_increment(const char* email, int max_per_day);

#endif