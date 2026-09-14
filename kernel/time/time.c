#include "time.h"
#include "rtc.h"
#include "../arch/x86_64/pit.h"
#include "../sched/scheduler.h"
#include <stdint.h>

static uint32_t hz;
static uint64_t boot_epoch;
static rix_waitqueue_t sleep_wq;

int time_init(uint32_t tick_hz) {
    if (!tick_hz) return -1;
    hz=tick_hz;
    rix_waitqueue_init(&sleep_wq);
    boot_epoch=rtc_unix_seconds();
    return boot_epoch ? 0 : -2;
}
uint64_t time_ticks(void) { return pit_ticks(); }
uint64_t time_monotonic_ns(void) {
    if (!hz) return 0;
    uint64_t ticks=pit_ticks();
    if (ticks > UINT64_MAX/1000000000ULL) return UINT64_MAX;
    return (ticks*1000000000ULL)/hz;
}
int time_realtime(rix_timespec_t *out) {
    if (!out || !hz || !boot_epoch) return -1;
    uint64_t ns=time_monotonic_ns();
    out->sec=boot_epoch+ns/1000000000ULL;
    out->nsec=ns%1000000000ULL;
    return 0;
}
rix_waitqueue_t *time_sleep_queue(void) { return &sleep_wq; }
void time_sleep_tick(void) {
    /* Lock-free occupancy peek: stale-zero skips one wake (the next tick,
     * <=10 ms later, re-evaluates — never a hang); stale-nonzero costs one
     * redundant wake_all. Keeps the common no-sleeper tick at ~zero cost
     * instead of scanning waiter slots + tasks under sched_lock at 100 Hz. */
    if (!sleep_wq.count) return;
    scheduler_wake_queue(&sleep_wq);
}
