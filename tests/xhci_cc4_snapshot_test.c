/* Host test: CC=4 snapshot forensics decode a crafted failing endpoint.
 *
 * Builds a fake controller (MMIO + output device context + transfer
 * ring in host memory), fires xhc_cc4_snapshot on a DCI-3 interrupt-IN
 * endpoint, and asserts every decoded field. Also pins the 5s throttle
 * (second immediate call is silent) and the cold-start rule (the very
 * first call always fires). The QEMU device_del run exercises the same
 * path against live emulation.
 */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../kernel/usb/xhci/xhc.h"

/* ---- Stubs + captured serial ---- */
rix_xhci_controller_t xhc_controllers[XHCI_MAX];
xhci_runtime_t xhc_runtimes[XHCI_MAX];
size_t xhc_count;
uint64_t xhc_scratchpad_array_phys[XHCI_MAX];

static uint64_t stub_now;
uint64_t time_monotonic_ns(void) { return stub_now; }

static char logbuf[8192];
static size_t logn;
void serial_write(const char *s) {
    while (*s && logn + 1u < sizeof(logbuf)) logbuf[logn++] = *s++;
    logbuf[logn] = 0;
}
void serial_write_hex(uint64_t v) {
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "[%llx]", (unsigned long long)v);
    serial_write(tmp);
}
void serial_write_dec(uint64_t v) {
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "<%llu>", (unsigned long long)v);
    serial_write(tmp);
}
static void log_reset(void) {
    logn = 0;
    logbuf[0] = 0;
}
static int log_has(const char *s) { return strstr(logbuf, s) != 0; }

static uint64_t count_hits(const char *s) {
    uint64_t n = 0;
    const char *p = logbuf;
    size_t len = strlen(s);
    while ((p = strstr(p, s)) != 0) {
        n++;
        p += len;
    }
    return n;
}

/* 16-byte aligned ring + context pages, as the driver requires. */
static rix_xhci_trb_t ring[64] __attribute__((aligned(16)));
static uint32_t devctx[32];
static uint8_t mmio[8192];

int main(void) {
    uint64_t ring_phys;
    uint64_t first;
    memset(ring, 0, sizeof(ring));
    memset(devctx, 0, sizeof(devctx));
    memset(mmio, 0, sizeof(mmio));
    memset(xhc_controllers, 0, sizeof(xhc_controllers));
    memset(xhc_runtimes, 0, sizeof(xhc_runtimes));
    xhc_count = 1;

    xhc_controllers[0].mmio_va = (uint64_t)(uintptr_t)mmio;
    xhc_controllers[0].cap_length = 0x20u;
    xhc_controllers[0].max_ports = 4u;
    xhc_controllers[0].max_slots = 8u;
    xhc_controllers[0].hcc_params1 = 0u;
    xhc_controllers[0].running = 1;

    /* PORTSC port 1: CCS|PED|PP|speed1, PLS U0. */
    *(volatile uint32_t *)(mmio + 0x20u + 0x400u) =
        (1u << 0) | (1u << 1) | (1u << 9) | (1u << 10);
    /* USBSTS: running, no error. */
    *(volatile uint32_t *)(mmio + 0x20u + 0x04u) = 0u;

    /* Slot context: speed 1, entries 3, RH port 1, addr 5, ADDRESSED. */
    devctx[0] = (1u << 20) | (3u << 27);
    devctx[1] = 1u << 16;
    devctx[3] = 5u | (2u << 27);
    /* DCI 3 output EP context: Running, CErr 3, INT_IN, MPS 8,
     * interval 6, ESIT-lo 8, avg 8, deq ring|DCS. */
    ring_phys = (uint64_t)(uintptr_t)ring;
    assert((ring_phys & 0xfu) == 0u);
    devctx[24] = 1u | (6u << 16);
    devctx[25] = (3u << 1) | (7u << 3) | (8u << 16);
    devctx[26] = (uint32_t)ring_phys | 1u;
    devctx[27] = (uint32_t)(ring_phys >> 32);
    devctx[28] = (8u << 16) | 8u;

    xhc_runtimes[0].slots[1].device_context_phys =
        (uint64_t)(uintptr_t)devctx;
    xhc_runtimes[0].slots[1].port = 1;
    xhc_runtimes[0].slots[1].endpoints[3].ring_phys = ring_phys;
    xhc_runtimes[0].slots[1].endpoints[3].enqueue = 5;
    xhc_runtimes[0].slots[1].endpoints[3].cycle = 1;

    /* Failing TD at index 4: Normal|IOC|ISP, producer cycle 1. */
    ring[4].parameter_lo = 0xabcd4688u;
    ring[4].parameter_hi = 0u;
    ring[4].status = 64u;
    ring[4].control =
        (1u << 10) | (1u << 5) | (1u << 2) | (1u << 0);
    first = ring_phys + 4u * sizeof(ring[0]);

    /* Seed one doorbell + one transfer event for this endpoint. */
    xhc_record_doorbell(0, 1, 3, 3);
    xhc_record_event(0, 32, (4u << 24) | 64u, 1, 3, first);

    /* Cold start: must fire even with the clock near zero. */
    stub_now = 1000u;
    log_reset();
    xhc_cc4_snapshot(0, &xhc_runtimes[0], 1, 3, first, first);
    assert(log_has("CC4-SNAPSHOT"));
    assert(log_has("ep=<3>"));
    assert(log_has("xfer param=[abcd4688]"));
    assert(log_has("ep state=<1>"));
    assert(log_has("cerr=<3>"));
    assert(log_has("type=<7>"));
    assert(log_has("mps=<8>"));
    assert(log_has("burst=<0>"));
    assert(log_has("interval=<6>"));
    assert(log_has("mult=<0>"));
    assert(log_has("esit=<8>"));
    assert(log_has("avg=<8>"));
    assert(log_has("dcs=<1>"));
    assert(log_has("ring cycle=<1>"));
    assert(log_has("trb-cycle=<1>"));
    assert(log_has("CCS=<1>"));
    assert(log_has("PED=<1>"));
    assert(log_has("PLS=<0>"));
    assert(log_has("PP=<1>"));
    assert(log_has("speed=<1>"));
    assert(log_has("HCH=<0>"));
    assert(log_has("HCE=<0>"));
    assert(log_has("last-db v=[3]"));
    assert(log_has("resid=[40]"));
    assert(log_has("cc=<4>"));

    /* Immediate second call: throttled silent. */
    log_reset();
    xhc_cc4_snapshot(0, &xhc_runtimes[0], 1, 3, first, first);
    assert(count_hits("CC4-SNAPSHOT") == 0u);

    /* 6s later: fires again. */
    stub_now += 6000000000ULL;
    log_reset();
    xhc_cc4_snapshot(0, &xhc_runtimes[0], 1, 3, first, first);
    assert(count_hits("CC4-SNAPSHOT") == 1u);

    printf("xhci-cc4-snapshot: OK\n");
    return 0;
}
