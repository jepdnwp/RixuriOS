/* Host test: 64 KiB boundary math for transfer buffers (xHCI 4.11.7.1).
 * A single data TRB must not span a 64 KiB boundary; the driver keeps
 * single-TRB TDs and aligns its buffers instead of scattering like
 * Linux queue_bulk_tx. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "../kernel/usb/xhci/xhc.h"

int main(void) {
    assert(xhc_pa_crosses_64k(0x10000u, 1u) == 0);
    assert(xhc_pa_crosses_64k(0x1FFFFu, 1u) == 0);
    assert(xhc_pa_crosses_64k(0x1FFFFu, 2u) == 1);
    assert(xhc_pa_crosses_64k(0x10000u, 0x10000u) == 0);
    assert(xhc_pa_crosses_64k(0x10000u, 0x10001u) == 1);
    assert(xhc_pa_crosses_64k(0u, 0u) == 0);
    assert(xhc_pa_crosses_64k(0xFFFF0000u, 0x20000u) == 1);
    assert(xhc_pa_crosses_64k(UINT64_MAX - 10u, 20u) == 1);
    assert(xhc_pa_crosses_64k(0x12345678u, 64u) == 0);
    /* The lucky build: kbd buffer inside one page. */
    assert(xhc_pa_crosses_64k(0x40680C0u, 64u) == 0);
    /* Same buffer shifted to straddle: must trip. */
    assert(xhc_pa_crosses_64k(0x40FFFD0u, 64u) == 1);
    /* Size-aligned objects never cross (the driver invariant). */
    assert(xhc_pa_crosses_64k(0x4068D40u, 64u) == 0);
    printf("xhci-dma-boundary: OK\n");
    return 0;
}
