/* Host test: Linux-derived endpoint Interval encoding.
 *
 * Covers xhc_ep_interval() (kernel/usb/xhci/ep.c) against the Linux
 * xhci_get_endpoint_interval rules: FS/LS interrupt bInterval is in
 * frames (fls(bInterval*8)-1 clamped 3..10), HS/SS interrupt bInterval
 * is already an exponent (clamp 1..16 minus 1), bulk/control take 0.
 * A raw bInterval programmed directly (e.g. FS 10) would schedule every
 * 2^10 microframes instead of ~8ms on real silicon; QEMU ignores the
 * field, so only this test guards the encoding.
 */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "../kernel/usb/xhci/xhc.h"

/* ---- Minimal stubs so ep.c links on host (never called) ---- */
rix_xhci_controller_t xhc_controllers[XHCI_MAX];
xhci_runtime_t xhc_runtimes[XHCI_MAX];
size_t xhc_count;
uint64_t xhc_scratchpad_array_phys[XHCI_MAX];
uint64_t xhc_dma_page(const rix_xhci_controller_t *c) { (void)c; return 0; }
void xhc_zero_page(uint64_t phys) { (void)phys; }
uint64_t pmm_alloc_page_below(uint64_t m) { (void)m; return 0; }
void pmm_free_page(uint64_t p) { (void)p; }
int xhc_submit_command(size_t c, uint64_t p, uint32_t q, uint8_t *s) {
    (void)c; (void)p; (void)q; (void)s; return -1;
}
int xhc_wait_transfer(size_t c, xhci_runtime_t *r, uint64_t f, uint64_t l,
                      uint8_t s, uint8_t e, uint16_t q, uint16_t *a) {
    (void)c; (void)r; (void)f; (void)l; (void)s; (void)e; (void)q; (void)a;
    return -1;
}
void xhc_record_doorbell(size_t c, uint8_t s, uint8_t e, uint32_t v) {
    (void)c; (void)s; (void)e; (void)v;
}
uint64_t xhc_dma_linear_pa(const void *b, uint64_t l) { (void)b; (void)l; return 0; }
void serial_write(const char *s) { (void)s; }
void serial_write_hex(uint64_t v) { (void)v; }
void serial_write_dec(uint64_t v) { (void)v; }

int main(void) {
    /* Full/low-speed interrupt: frames -> log2 microframes. */
    assert(xhc_ep_interval(1u, 3u, 1u) == 3u);
    assert(xhc_ep_interval(1u, 3u, 10u) == 6u);
    assert(xhc_ep_interval(1u, 3u, 8u) == 6u);
    assert(xhc_ep_interval(1u, 3u, 255u) == 10u);
    /* bInterval 0 is descriptor-invalid (rejected by configure); the
     * verbatim Linux clamp still yields 10 here, pinned as-is. */
    assert(xhc_ep_interval(1u, 3u, 0u) == 10u);
    assert(xhc_ep_interval(2u, 3u, 10u) == 6u);
    /* High-speed / SuperSpeed interrupt: exponent passthrough. */
    assert(xhc_ep_interval(3u, 3u, 1u) == 0u);
    assert(xhc_ep_interval(3u, 3u, 4u) == 3u);
    assert(xhc_ep_interval(3u, 3u, 10u) == 9u);
    assert(xhc_ep_interval(3u, 3u, 16u) == 15u);
    assert(xhc_ep_interval(3u, 3u, 20u) == 15u);
    assert(xhc_ep_interval(4u, 3u, 9u) == 8u);
    assert(xhc_ep_interval(5u, 3u, 1u) == 0u);
    /* Bulk/control and unknown speeds take field 0, except HS bulk
     * which carries the Max NAK rate exponent. */
    assert(xhc_ep_interval(1u, 2u, 10u) == 0u);
    assert(xhc_ep_interval(3u, 2u, 0u) == 0u);
    assert(xhc_ep_interval(3u, 2u, 1u) == 0u);
    assert(xhc_ep_interval(3u, 2u, 8u) == 3u);
    assert(xhc_ep_interval(3u, 2u, 255u) == 7u);
    assert(xhc_ep_interval(9u, 3u, 10u) == 0u);
    assert(xhc_ep_interval(1u, 0u, 10u) == 0u);
    printf("xhci-ep-interval: OK\n");
    return 0;
}
