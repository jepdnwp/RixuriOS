/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Linux-derived xHCI TRB / context definitions for RixuriOS.
 *
 * Provenance: drivers/usb/host/xhci.h (torvalds/linux, master, 2026-09-16
 * window) — union xhci_trb, TRB type IDs, transfer-event / command-
 * completion fields, slot/endpoint/input-control context layouts, and the
 * completion-code table. Only the parts the running driver needs are
 * carried; streams, isoch, bandwidth-negotiation and vendor TRBs are
 * explicitly excluded (single-user desktop scope).
 *
 * Adaptations: __le* -> rix_le* identity types; bit/field macros keep
 * Linux names/values so behavior reviews can diff against Linux.
 * Verified against kernel/usb/xhci.c TRB defines (LINK/NORMAL/SETUP/
 * DATA/STATUS/TRANSFER_EVENT/COMMAND_COMPLETION/ENABLE_SLOT/ADDRESS_DEVICE
 * and XHCI_COMPLETION_* codes).
 */

#pragma once

#include <stdint.h>

/* A TRB is always 16 bytes. Written little-endian. */
typedef struct {
    uint32_t parameter_lo;
    uint32_t parameter_hi;
    uint32_t status;
    uint32_t control;
} rix_xhci_trb_t;

typedef struct {
    uint64_t ring_segment_base;
    uint32_t ring_segment_size;
    uint32_t reserved;
} rix_xhci_erst_entry_t;

/* TRB type field: bits 15:10 of the control dword. */
#define XHCI_TRB_TYPE_SHIFT 10u
#define XHCI_TRB_TYPE_MASK (0x3fu << 10)
#define XHCI_TRB_TYPE(p) ((uint32_t)(p) << 10)
#define XHCI_TRB_FIELD_TO_TYPE(p) (((p) & XHCI_TRB_TYPE_MASK) >> 10)

#define XHCI_TRB_NORMAL 1u
#define XHCI_TRB_SETUP_STAGE 2u
#define XHCI_TRB_DATA_STAGE 3u
#define XHCI_TRB_STATUS_STAGE 4u
#define XHCI_TRB_LINK 6u
#define XHCI_TRB_ENABLE_SLOT 9u
#define XHCI_TRB_DISABLE_SLOT 10u
#define XHCI_TRB_ADDRESS_DEVICE 11u
#define XHCI_TRB_CONFIGURE_ENDPOINT 12u
#define XHCI_TRB_EVALUATE_CONTEXT 13u
#define XHCI_TRB_RESET_ENDPOINT 14u
#define XHCI_TRB_SET_DEQUEUE_POINTER 16u
#define XHCI_TRB_TRANSFER_EVENT 32u
#define XHCI_TRB_COMMAND_COMPLETION 33u
#define XHCI_TRB_PORT_STATUS_CHANGE 34u

/* Control-dword flags (Linux TRB_CYCLE/TRB_CHAIN/TRB_IOC/TRB_IDT/...). */
#define XHCI_TRB_CYCLE (1u << 0)
#define XHCI_TRB_ENT (1u << 1)
#define XHCI_TRB_ISP (1u << 2)
#define XHCI_TRB_CHAIN (1u << 4)
#define XHCI_TRB_IOC (1u << 5)
#define XHCI_TRB_IDT (1u << 6)
#define XHCI_TRB_TC (1u << 1)
#define XHCI_TRB_TSP (1u << 9)
#define XHCI_TRB_DIR (1u << 16)

/* Command-TRB routing fields. */
#define XHCI_TRB_EP_SHIFT 16u
#define XHCI_TRB_SLOT_SHIFT 24u
#define XHCI_TRB_TO_SLOT_ID(p) (((p) >> 24) & 0xffu)
#define XHCI_TRB_SLOT_FOR(p) (((uint32_t)(p) & 0xffu) << 24)
#define XHCI_TRB_TO_EP_ID(p) (((p) >> 16) & 0x1fu)

/* Transfer-event length / completion code (Linux EVENT_TRB_LEN/GET_COMP_CODE). */
#define XHCI_EVENT_TRB_LEN(p) ((p) & 0xffffffu)
#define XHCI_COMP_CODE_MASK (0xffu << 24)
#define XHCI_GET_COMP_CODE(p) (((p) & XHCI_COMP_CODE_MASK) >> 24)

#define XHCI_COMP_INVALID 0u
#define XHCI_COMP_SUCCESS 1u
#define XHCI_COMP_DATA_BUFFER_ERROR 2u
#define XHCI_COMP_BABBLE 3u
#define XHCI_COMP_USB_TRANSACTION_ERROR 4u
#define XHCI_COMP_TRB_ERROR 5u
#define XHCI_COMP_STALL_ERROR 6u
#define XHCI_COMP_RESOURCE_ERROR 7u
#define XHCI_COMP_BANDWIDTH_ERROR 8u
#define XHCI_COMP_NO_SLOTS_AVAILABLE 9u
#define XHCI_COMP_INVALID_STREAM_TYPE 10u
#define XHCI_COMP_SLOT_NOT_ENABLED 11u
#define XHCI_COMP_ENDPOINT_NOT_ENABLED 12u
#define XHCI_COMP_SHORT_PACKET 13u
#define XHCI_COMP_RING_UNDERRUN 14u
#define XHCI_COMP_RING_OVERRUN 15u
#define XHCI_COMP_PARAMETER_ERROR 17u
#define XHCI_COMP_CONTEXT_STATE_ERROR 19u
#define XHCI_COMP_EVENT_RING_FULL 21u
#define XHCI_COMP_MISSED_SERVICE 23u
#define XHCI_COMP_COMMAND_RING_STOPPED 24u
#define XHCI_COMP_COMMAND_ABORTED 25u
#define XHCI_COMP_STOPPED 26u

/* Slot context (Linux struct xhci_slot_ctx, 32-byte view). */
#define XHCI_SLOT_LAST_CTX_SHIFT 27u
#define XHCI_SLOT_LAST_CTX_MASK (0x1fu << 27)
#define XHCI_SLOT_SPEED_SHIFT 20u
#define XHCI_SLOT_SPEED_MASK (0xfu << 20)
#define XHCI_SLOT_STATE_SHIFT 27u

#define XHCI_SLOT_STATE_DISABLED 0u
#define XHCI_SLOT_STATE_DEFAULT 1u
#define XHCI_SLOT_STATE_ADDRESSED 2u
#define XHCI_SLOT_STATE_CONFIGURED 3u

/* Endpoint context type values (Linux EP_TYPE). */
#define XHCI_EP_BULK_OUT 2u
#define XHCI_EP_CONTROL 4u
#define XHCI_EP_BULK_IN 6u
#define XHCI_EP_INTERRUPT_IN 7u
#define XHCI_EP_INTERRUPT_OUT 3u

/* Input Control Context Add flags (Linux ADD_EP/DROP_EP, xHCI 6.2.5.1). */
#define XHCI_INPUT_ADD_SLOT (1u << 0)
#define XHCI_INPUT_ADD_EP0 (1u << 1)

/* Ring geometry carried from Linux (TRBS_PER_SEGMENT/TRB_SEGMENT_SIZE). */
/* RixuriOS early driver uses single-page rings, not 256-TRB segments:
 * the Linux segment size is documented here for review, not allocated. */
#define XHCI_TRBS_PER_SEGMENT 256u
#define XHCI_TRB_SEGMENT_SIZE (256u * 16u)
#define XHCI_CMD_RING_TRBS 64u
#define XHCI_EVENT_RING_TRBS 64u
