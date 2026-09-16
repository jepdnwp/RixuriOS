/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Linux-derived xHCI register definitions for RixuriOS.
 *
 * Provenance: drivers/usb/host/xhci.h (torvalds/linux, master, 2026-09-16
 * window) — struct xhci_cap_regs, struct xhci_op_regs (without the
 * variable-length port_regs tail; RixuriOS computes PORTSC addresses by
 * stride), struct xhci_intr_reg, struct xhci_run_regs (single-interrupter
 * view; RixuriOS is a single-user desktop OS and uses interrupter 0 in
 * polling mode), struct xhci_doorbell_array, and the USBCMD/USBSTS/CRCR/
 * CONFIG/IMAN/ERST bit definitions used by setup_runtime/reset paths.
 *
 * Adaptations (behavior preserved, Linux abstractions removed):
 *   __le32/__le64 -> plain uint32_t/uint64_t (x86_64 is little-endian;
 *       TRB/register layouts kept explicit); __iomem dropped (RixuriOS
 *       uses volatile uint8_t base + offsets, as in this driver's core.c).
 *   No struct usb_hcd / pci_driver glue (Linux USB core/PCI framework is
 *       NOT ported; RixuriOS PCI is kernel/pci/pci.h).
 *   No power-management / suspend-resume bits beyond CMD_RUN/CMD_RESET/
 *       CMD_EIE handling the running driver needs.
 *
 * Verified against the previous verified driver's XHCI_USBCMD/XHCI_USBSTS/
 *   XHCI_CRCR/XHCI_DCBAAP/XHCI_CONFIG offsets and XHCI_STS_* / XHCI_CMD_* bits.
 */

#pragma once

#include <stdint.h>

/* ---- Capability registers (xHCI 5.3, cf. Linux struct xhci_cap_regs) ---- */

#define XHCI_CAPLENGTH 0x00u
#define XHCI_HCIVERSION 0x02u
#define XHCI_HCSPARAMS1 0x04u
#define XHCI_HCSPARAMS2 0x08u
#define XHCI_HCSPARAMS3 0x0cu
#define XHCI_HCCPARAMS1 0x10u
#define XHCI_DBOFF 0x14u
#define XHCI_RTSOFF 0x18u
#define XHCI_HCCPARAMS2 0x1cu

typedef struct {
    uint32_t hc_capbase;
    uint32_t hcs_params1;
    uint32_t hcs_params2;
    uint32_t hcs_params3;
    uint32_t hcc_params;
    uint32_t db_off;
    uint32_t run_regs_off;
    uint32_t hcc_params2;
} rix_xhci_cap_regs_t;

/* HCSPARAMS1 fields (Linux MAX_HC_SLOTS/MAX_HC_PORTS/MAX_HC_INTRS). */
#define XHCI_MAX_SLOTS_MASK 0xffu
#define XHCI_MAX_INTRS_SHIFT 8u
#define XHCI_MAX_INTRS_MASK (0x7ffu << 8)
#define XHCI_MAX_PORTS_SHIFT 24u
#define XHCI_MAX_PORTS_MASK (0xffu << 24)

/* HCCPARAMS1 fields used by the driver. */
#define XHCI_HCC_AC64 (1u << 0)
#define XHCI_HCC_CSZ (1u << 2)
#define XHCI_HCC_PPC (1u << 3)
#define XHCI_HCC_XECP_SHIFT 16u
#define XHCI_HCC_XECP_MASK (0xffffu << 16)

/* ---- Operational registers (xHCI 5.4, cf. Linux struct xhci_op_regs) ---- */

#define XHCI_USBCMD 0x00u
#define XHCI_USBSTS 0x04u
#define XHCI_PAGESIZE 0x08u
#define XHCI_DNCTRL 0x14u
#define XHCI_CRCR 0x18u
#define XHCI_DCBAAP 0x30u
#define XHCI_CONFIG 0x38u
#define XHCI_PORTSC_BASE 0x400u
#define XHCI_PORT_STRIDE 0x10u

/* USBCMD bits (Linux CMD_RUN/CMD_RESET/CMD_EIE/CMD_HSEIE). */
#define XHCI_CMD_RUN (1u << 0)
#define XHCI_CMD_HCRST (1u << 1)
#define XHCI_CMD_INTE (1u << 2)

/* USBSTS bits (Linux STS_HALT/STS_FATAL/STS_EINT/STS_PORT/STS_CNR/STS_HCE). */
#define XHCI_STS_HCH (1u << 0)
#define XHCI_STS_HSE (1u << 2)
#define XHCI_STS_EINT (1u << 3)
#define XHCI_STS_PCD (1u << 4)
#define XHCI_STS_CNR (1u << 11)
#define XHCI_STS_HCE (1u << 12)
#define XHCI_STS_W1C_MASK \
    (XHCI_STS_HSE | XHCI_STS_EINT | XHCI_STS_PCD)

/* CRCR bits (Linux CMD_RING_*). Bit 0 is the cycle bit. */
#define XHCI_CRCR_CYCLE (1ULL << 0)
#define XHCI_CRCR_PTR_MASK (~0x3fULL)

/* CONFIG bits (Linux MAX_DEVS). */
#define XHCI_CONFIG_SLOTS_MASK 0xffu

/* ---- Runtime / interrupter registers (xHCI 5.5, Linux struct xhci_intr_reg) ---- */

#define XHCI_IMAN_IP (1u << 0)
#define XHCI_IMAN_IE (1u << 1)
#define XHCI_IMOD 0x24u
#define XHCI_ERSTSZ_OFF 0x28u
#define XHCI_ERSTBA_OFF 0x30u
#define XHCI_ERDP_OFF 0x38u
#define XHCI_ERDP_EHB (1ULL << 3)
#define XHCI_ERDP_PTR_MASK (~0xfull)
#define XHCI_IMAN_OFF 0x20u

typedef struct {
    uint32_t iman;
    uint32_t imod;
    uint32_t erst_size;
    uint32_t rsvd;
    uint64_t erst_base;
    uint64_t erst_dequeue;
} rix_xhci_intr_reg_t;

/* ---- Doorbells (xHCI 5.6, Linux struct xhci_doorbell_array) ---- */

#define XHCI_DB_TARGET(ep) ((((ep) + 1u) & 0xffu))
#define XHCI_DB_HOST 0x00000000u

/* ---- Extended capability IDs (Linux xhci-ext-caps.h) ----
 * NOTE: XHCI_EXT_CAP_ID_PROTOCOL lives in caps.h (canonical home, used by
 * the host test); do not duplicate it here. */

#define XHCI_EXT_CAP_ID_LEGACY 0x01u
#define XHCI_USBLSUP_BIOS_OWNED (1u << 16)
#define XHCI_USBLSUP_OS_OWNED (1u << 24)
#define XHCI_EXT_CAP_WALK_MAX 64u

/* ---- BAR mapping bound (RixuriOS fail-closed, not Linux) ---- */

#define XHCI_BAR_MAP_MAX 0x1000000ULL
