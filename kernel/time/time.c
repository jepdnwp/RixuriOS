#include "time.h"
#include "rtc.h"
#include "../arch/x86_64/pit.h"
#include <stdint.h>

static uint32_t hz;
static uint64_t boot_epoch;

int time_init(uint32_t tick_hz) {
    if (!tick_hz) return -1;
    hz=tick_hz;
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
