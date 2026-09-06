#pragma once
#include <stdint.h>

typedef struct { uint64_t sec; uint64_t nsec; } rix_timespec_t;

int time_init(uint32_t tick_hz);
uint64_t time_monotonic_ns(void);
int time_realtime(rix_timespec_t *out);
uint64_t time_ticks(void);
