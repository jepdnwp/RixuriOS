/* SPDX-License-Identifier: GPL-2.0 */
/*
 * USB chapter 9 definitions for RixuriOS.
 *
 * Provenance: include/uapi/linux/usb/ch9.h (torvalds/linux, master,
 * 2026-09-16 window) — descriptor type codes, descriptor sizes, standard
 * request codes, bmRequestType masks, endpoint macros. HID class codes
 * (0x21/0x22/0x23) come from include/uapi/linux/hid.h. Only the
 * single-user desktop subset is carried: no wireless (WUSB), no OTG/SRP,
 * no BOS/capability parsing, no isochronous audio extensions.
 *
 * Adaptations: __u8/__u16/__le16 struct fields are NOT carried — the
 * RixuriOS parser (kernel/usb/usb.c) reads wire bytes positionally with
 * an explicit little-endian accessor, which is safer under the strict
 * aliasing rules this kernel builds with (no -fno-strict-aliasing, so
 * packed-struct casts over DMA buffers are avoided by design).
 * RixuriOS keeps its own RIX_USB_* names (kernel/usb/usb.h); the
 * _Static_asserts there bind them to these Linux values at compile time.
 */

#pragma once

#include <stdint.h>

/* USB_DT_* descriptor types (USB 2.0 §9.4). */
#define USB_DT_DEVICE 0x01u
#define USB_DT_CONFIG 0x02u
#define USB_DT_STRING 0x03u
#define USB_DT_INTERFACE 0x04u
#define USB_DT_ENDPOINT 0x05u
#define USB_DT_DEVICE_QUALIFIER 0x06u
#define USB_DT_OTHER_SPEED_CONFIG 0x07u
#define USB_DT_INTERFACE_POWER 0x08u
#define USB_DT_OTG 0x09u
#define USB_DT_DEBUG 0x0Au
#define USB_DT_INTERFACE_ASSOCIATION 0x0Bu
#define USB_DT_SECURITY 0x0Cu
#define USB_DT_KEY 0x0Du
#define USB_DT_ENCRYPTION_TYPE 0x0Eu
#define USB_DT_BOS 0x0Fu
#define USB_DT_DEVICE_CAPABILITY 0x10u
#define USB_DT_WIRELESS_ENDPOINT_COMP 0x11u
#define USB_DT_SS_ENDPOINT_COMP 0x30u

/* HID class descriptor types (USB HID 1.11 §7.1, uapi hid.h). */
#define HID_DT_HID 0x21u
#define HID_DT_REPORT 0x22u
#define HID_DT_PHYSICAL 0x23u
#define USB_DT_CS_INTERFACE 0x24u
#define USB_DT_CS_ENDPOINT 0x25u

/* Descriptor sizes. */
#define USB_DT_DEVICE_SIZE 18u
#define USB_DT_CONFIG_SIZE 9u
#define USB_DT_INTERFACE_SIZE 9u
#define USB_DT_ENDPOINT_SIZE 7u
#define USB_DT_SS_EP_COMP_SIZE 6u

/* USB_REQ_* standard device requests (USB 2.0 §9.4). */
#define USB_REQ_GET_STATUS 0x00u
#define USB_REQ_CLEAR_FEATURE 0x01u
#define USB_REQ_SET_FEATURE 0x03u
#define USB_REQ_SET_ADDRESS 0x05u
#define USB_REQ_GET_DESCRIPTOR 0x06u
#define USB_REQ_SET_DESCRIPTOR 0x07u
#define USB_REQ_GET_CONFIGURATION 0x08u
#define USB_REQ_SET_CONFIGURATION 0x09u
#define USB_REQ_GET_INTERFACE 0x0Au
#define USB_REQ_SET_INTERFACE 0x0Bu
#define USB_REQ_SYNCH_FRAME 0x0Cu
#define USB_REQ_SET_SEL 0x30u
#define USB_REQ_SET_ISOCH_DELAY 0x31u

/* bmRequestType: direction (bit 7). */
#define USB_DIR_OUT 0x00u
#define USB_DIR_IN 0x80u

/* bmRequestType: type (bits 6:5). */
#define USB_TYPE_MASK 0x60u
#define USB_TYPE_STANDARD 0x00u
#define USB_TYPE_CLASS 0x20u
#define USB_TYPE_VENDOR 0x40u
#define USB_TYPE_RESERVED 0x60u

/* bmRequestType: recipient (bits 4:0). */
#define USB_RECIP_MASK 0x1Fu
#define USB_RECIP_DEVICE 0x00u
#define USB_RECIP_INTERFACE 0x01u
#define USB_RECIP_ENDPOINT 0x02u
#define USB_RECIP_OTHER 0x03u

/* Endpoint descriptor: bEndpointAddress. */
#define USB_ENDPOINT_NUMBER_MASK 0x0Fu
#define USB_ENDPOINT_DIR_MASK 0x80u

/* Endpoint descriptor: bmAttributes transfer type (bits 1:0). */
#define USB_ENDPOINT_XFERTYPE_MASK 0x03u
#define USB_ENDPOINT_XFER_CONTROL 0u
#define USB_ENDPOINT_XFER_ISOC 1u
#define USB_ENDPOINT_XFER_BULK 2u
#define USB_ENDPOINT_XFER_INT 3u

/* Endpoint descriptor: wMaxPacketSize (bits 10:0). */
#define USB_ENDPOINT_MAXP_MASK 0x07FFu

/* Feature selectors. */
#define USB_ENDPOINT_HALT 0u
