#pragma once
#include <stdint.h>

typedef int64_t time_t;
typedef int32_t clock_t;
typedef int clockid_t;
struct tm {
    int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year;
    int tm_wday, tm_yday, tm_isdst;
};

/* POSIX clock deterrent. Layout matches the 16-byte kernel timespec
 * (sec, nsec as consecutive 64-bit words), so the pointer is passed
 * straight to RIX_SYS_CLOCK_GETTIME / RIX_SYS_NANOSLEEP. rix_timespec_t
 * in <unistd.h> is the legacy native spelling of the same layout. */
struct timespec {
    time_t tv_sec;
    long tv_nsec;
};

int clock_gettime(clockid_t clock, struct timespec *out);
int nanosleep(const struct timespec *request, struct timespec *remaining);

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
