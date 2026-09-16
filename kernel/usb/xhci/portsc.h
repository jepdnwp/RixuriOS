#pragma once
#include <stdint.h>

/* Pure PORTSC (xHCI 5.4.8) bit definitions and write-composition helpers.
 * No MMIO in this header by design: the caller passes a raw register
 * value (volatile or test fixture) and gets back the value to write, so
 * the RW1C/RW1S/RWS semantics below are fully host-testable.
 *
 * A raw PORTSC readback must NEVER be written back verbatim. The bits
 * with write side effects are:
 *   PED (1)   RW1CS: writing 1 clears the bit = DISABLES the port
 *   PR  (4)   RW1S:  writing 1 starts a port reset
 *   PIC (15:14) RW1CS: port indicators
 *   LWS (16)  RW:    writing 1 latches the PLS write
 *   CSC..CEC (23:17) RW1C: writing 1 clears the change bit
 *   WPR (31)  RW1S:  writing 1 starts a warm port reset
 *   reserved  RsvdZ: software must write 0
 * Round-tripping a readback that has PED=1 therefore disables a port that
 * hardware had just enabled — the classic "reset completed, then the port
 * went back to Disabled (PED=0, PLS=Polling)" attach failure.
 */
#define XHCI_PORT_CCS (1u << 0)   /* R: current connect status */
#define XHCI_PORT_PED (1u << 1)   /* RW1CS: write 1 disables port */
#define XHCI_PORT_OCA (1u << 3)   /* R: over-current active */
#define XHCI_PORT_PR  (1u << 4)   /* RW1S: write 1 starts reset */
#define XHCI_PORT_PLS_MASK (0xFu << 5) /* RWS, latched by LWS */
#define XHCI_PORT_PP  (1u << 9)   /* RWS: port power */
#define XHCI_PORT_SPEED_SHIFT 10
#define XHCI_PORT_SPEED_MASK (0xFu << XHCI_PORT_SPEED_SHIFT) /* R USB2 / RWS USB3 */
#define XHCI_PORT_PIC_MASK (0x3u << 14) /* RW1CS: port indicators */
#define XHCI_PORT_LWS (1u << 16)  /* RW: 1 latches PLS write */
#define XHCI_PORT_CSC (1u << 17)  /* RW1C */
#define XHCI_PORT_PEC (1u << 18)  /* RW1C */
#define XHCI_PORT_WRC (1u << 19)  /* RW1C (USB3 warm reset complete) */
#define XHCI_PORT_OCC (1u << 20)  /* RW1C */
#define XHCI_PORT_PRC (1u << 21)  /* RW1C (port reset complete) */
#define XHCI_PORT_PLC (1u << 22)  /* RW1C */
#define XHCI_PORT_CEC (1u << 23)  /* RW1C */
#define XHCI_PORT_CAS (1u << 24)  /* R: cold attach status */
#define XHCI_PORT_WCE (1u << 25)  /* RWS: wake on connect enable */
#define XHCI_PORT_WDE (1u << 26)  /* RWS: wake on disconnect enable */
#define XHCI_PORT_WOE (1u << 27)  /* RWS: wake on over-current enable */
#define XHCI_PORT_DR  (1u << 30)  /* R: device removable */
#define XHCI_PORT_WPR (1u << 31)  /* RW1S: write 1 starts warm reset */
/* All port-change W1C bits cleared by writing 1. */
#define XHCI_PORT_CHANGE_MASK (XHCI_PORT_CSC | XHCI_PORT_PEC | XHCI_PORT_WRC | \
                               XHCI_PORT_OCC | XHCI_PORT_PRC | XHCI_PORT_PLC | \
                               XHCI_PORT_CEC)
/* Bits that may round-trip through a PORTSC read-modify-write: the R/O
 * status bits plus the RWS state bits (PP, PLS). Everything else — PED,
 * PR, WPR, LWS, PIC, the change bits and reserved bits — must be dropped
 * from a raw readback and only re-added explicitly. Same principle as
 * Linux xhci_port_state_to_neutral(). */
#define XHCI_PORT_RO_MASK  (XHCI_PORT_CCS | XHCI_PORT_OCA | XHCI_PORT_SPEED_MASK | \
                            XHCI_PORT_CAS | XHCI_PORT_DR)
#define XHCI_PORT_RWS_MASK (XHCI_PORT_PLS_MASK | XHCI_PORT_PP)

/* Strip every write-side-effect bit from a raw PORTSC readback, leaving a
 * value that can be written back without changing port state. */
static inline uint32_t xhci_portsc_neutralize(uint32_t state) {
    return state & (XHCI_PORT_RO_MASK | XHCI_PORT_RWS_MASK);
}

/* Neutralize and clear all change bits (write 1 to each W1C bit). PED is
 * deliberately not carried over, so a just-enabled port stays enabled. */
static inline uint32_t xhci_portsc_clear_changes(uint32_t state) {
    return xhci_portsc_neutralize(state) | XHCI_PORT_CHANGE_MASK;
}
