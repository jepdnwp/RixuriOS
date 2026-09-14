#pragma once
#include <stdint.h>
#include "../sync/waitqueue.h"

typedef struct { uint64_t sec; uint64_t nsec; } rix_timespec_t;

int time_init(uint32_t tick_hz);
uint64_t time_monotonic_ns(void);
int time_realtime(rix_timespec_t *out);
uint64_t time_ticks(void);
/* Phase P3 backend: tick-driven sleep queue for nanosleep. Sleepers bind
 * here and block; the PIT tick wakes them (10 ms granularity, matching
 * the tick-based monotonic clock — no precision is lost vs yield-polling).
 * No shared guard with the tick exists (IRQ context), so the syscall uses
 * prepare -> recheck-deadline -> block; a tick landing in the window only
 * costs one extra tick because ticks are perpetual. */
rix_waitqueue_t *time_sleep_queue(void);
void time_sleep_tick(void);
