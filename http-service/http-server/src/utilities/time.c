#include "utilities.h"

#include <stdio.h>
#include <time.h>

void utc_timestamp(char* buffer, size_t size)
{
    static const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

    time_t now = time(NULL);
    struct tm tm_info;
    gmtime_r(&now, &tm_info);

    snprintf(buffer, size, "%02d-%s-%04d %02d:%02d UTC", tm_info.tm_mday, months[tm_info.tm_mon], tm_info.tm_year + 1900, tm_info.tm_hour, tm_info.tm_min);
}
