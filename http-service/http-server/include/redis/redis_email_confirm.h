#ifndef REDIS_EMAIL_CONFIRM_H
#define REDIS_EMAIL_CONFIRM_H

void redis_mark_email_confirmed(const char* email);
int redis_is_email_confirmed(const char* email);

#endif
