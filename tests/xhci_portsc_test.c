#include "../kernel/usb/xhci/portsc.h"
#include <assert.h>
#include <stdint.h>

/* PORTSC write-composition: a raw readback must never round-trip the
 * RW1CS/RW1S/RW bits (PED especially: writing 1 disables the port). */

static void test_neutralize_strips_side_effects(void) {
    /* Post-reset readback as hardware reports it: CCS=1 PED=1 PP=1
     * PLS=U0 speed=1 plus PEC+PRC change bits and stray reserved bits. */
    uint32_t raw = XHCI_PORT_CCS | XHCI_PORT_PED | XHCI_PORT_PP |
                   (1u << XHCI_PORT_SPEED_SHIFT) | XHCI_PORT_PEC |
                   XHCI_PORT_PRC | (1u << 2) | (1u << 28);
    uint32_t n = xhci_portsc_neutralize(raw);
    assert((n & XHCI_PORT_PED) == 0u);
    assert((n & XHCI_PORT_PR) == 0u);
    assert((n & XHCI_PORT_WPR) == 0u);
    assert((n & XHCI_PORT_LWS) == 0u);
    assert((n & XHCI_PORT_PIC_MASK) == 0u);
    assert((n & XHCI_PORT_CHANGE_MASK) == 0u);
    assert((n & (1u << 2)) == 0u);
    assert((n & (1u << 28)) == 0u);
}

static void test_neutralize_keeps_ro_and_rws(void) {
    uint32_t raw = XHCI_PORT_CCS | XHCI_PORT_OCA | XHCI_PORT_PP |
                   (2u << XHCI_PORT_SPEED_SHIFT) | (7u << 5) |
                   XHCI_PORT_CAS | XHCI_PORT_DR;
    uint32_t n = xhci_portsc_neutralize(raw);
    assert((n & XHCI_PORT_CCS) != 0u);
    assert((n & XHCI_PORT_OCA) != 0u);
    assert((n & XHCI_PORT_PP) != 0u);
    assert(((n & XHCI_PORT_SPEED_MASK) >> XHCI_PORT_SPEED_SHIFT) == 2u);
    assert(((n & XHCI_PORT_PLS_MASK) >> 5) == 7u);
    assert((n & XHCI_PORT_CAS) != 0u);
    assert((n & XHCI_PORT_DR) != 0u);
}

static void test_clear_changes_never_disables(void) {
    /* The exact failure state from the report: a USB2 port whose reset
     * just completed — CCS=1, PED=1, PP=1, PLS=U0, speed=1, PRC+PEC set.
     * The change-clear write must not carry PED=1 (which would disable
     * the port) and must carry 1s on every W1C change bit. */
    uint32_t post_reset = XHCI_PORT_CCS | XHCI_PORT_PED | XHCI_PORT_PP |
                          (1u << XHCI_PORT_SPEED_SHIFT) |
                          XHCI_PORT_PRC | XHCI_PORT_PEC;
    uint32_t w = xhci_portsc_clear_changes(post_reset);
    assert((w & XHCI_PORT_PED) == 0u);
    assert((w & XHCI_PORT_PR) == 0u);
    assert((w & XHCI_PORT_WPR) == 0u);
    assert((w & XHCI_PORT_LWS) == 0u);
    assert((w & XHCI_PORT_CHANGE_MASK) == XHCI_PORT_CHANGE_MASK);
    assert((w & XHCI_PORT_CCS) != 0u);
    assert((w & XHCI_PORT_PP) != 0u);
    /* PED write of 0 has no effect per RW1CS, so hardware keeps PED=1. */

    /* Pre-reset polling state (0x6e1): nothing to disable, clear works. */
    uint32_t polling = XHCI_PORT_CCS | XHCI_PORT_PP | (7u << 5) |
                       (1u << XHCI_PORT_SPEED_SHIFT);
    w = xhci_portsc_clear_changes(polling);
    assert((w & XHCI_PORT_PED) == 0u);
    assert((w & XHCI_PORT_CHANGE_MASK) == XHCI_PORT_CHANGE_MASK);
}

static void test_reset_start_composition(void) {
    /* Reset start from the 0x6e1 polling state: PR asserted, stale change
     * bits cleared, and nothing else. PED must not round-trip. */
    uint32_t polling = XHCI_PORT_CCS | XHCI_PORT_PP | (7u << 5) |
                       (1u << XHCI_PORT_SPEED_SHIFT) | XHCI_PORT_CSC;
    uint32_t w = xhci_portsc_neutralize(polling) | XHCI_PORT_CHANGE_MASK |
                 XHCI_PORT_PR;
    assert((w & XHCI_PORT_PR) != 0u);
    assert((w & XHCI_PORT_PED) == 0u);
    assert((w & XHCI_PORT_LWS) == 0u);
    assert((w & XHCI_PORT_WPR) == 0u);
    assert((w & XHCI_PORT_CHANGE_MASK) == XHCI_PORT_CHANGE_MASK);
    assert((w & XHCI_PORT_PP) != 0u);
}

static void test_warm_reset_composition(void) {
    uint32_t trained = XHCI_PORT_CCS | XHCI_PORT_PP |
                       (4u << XHCI_PORT_SPEED_SHIFT);
    uint32_t w = xhci_portsc_neutralize(trained) | XHCI_PORT_CHANGE_MASK |
                 XHCI_PORT_WPR;
    assert((w & XHCI_PORT_WPR) != 0u);
    assert((w & XHCI_PORT_PED) == 0u);
    assert((w & XHCI_PORT_PR) == 0u);
    assert((w & XHCI_PORT_LWS) == 0u);
}

int main(void) {
    test_neutralize_strips_side_effects();
    test_neutralize_keeps_ro_and_rws();
    test_clear_changes_never_disables();
    test_reset_start_composition();
    test_warm_reset_composition();
    return 0;
}
