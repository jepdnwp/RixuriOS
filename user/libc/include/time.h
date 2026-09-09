#pragma once
#include <stdint.h>

typedef int64_t time_t;
typedef int32_t clock_t;
struct tm {
    int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year;
    int tm_wday, tm_yday, tm_isdst;
};

time_t time(time_t *out);
struct tm *gmtime(const time_t *value);
struct tm *localtime(const time_t *value);
struct tm *gmtime_r(const time_t *value, struct tm *result);
struct tm *localtime_r(const time_t *value, struct tm *result);
time_t mktime(struct tm *value);
char *asctime(const struct tm *value);
char *ctime(const time_t *value);
char *asctime_r(const struct tm *value, char *buffer);
char *ctime_r(const time_t *value, char *buffer);
double difftime(time_t left, time_t right);

#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1
#define CLOCKS_PER_SEC 1000000
