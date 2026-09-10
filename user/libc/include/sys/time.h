#pragma once
#include <time.h>

/* Calendar/clock compatibility. time_t comes from <time.h>. */

typedef long suseconds_t;
struct timeval {
    time_t tv_sec;
    suseconds_t tv_usec;
};

int gettimeofday(struct timeval *out, void *unused_zone);
