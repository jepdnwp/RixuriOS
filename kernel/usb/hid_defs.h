/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * HID item-encoding definitions for RixuriOS.
 *
 * Provenance: include/linux/hid.h (item format/type/tag Kuwait, collection
 * types, usage pages) and the fetch_item()/hid_parser_* rules of
 * drivers/hid/hid-core.c (torvalds/linux, master, 2026-09-16 window).
 * Only what the report-descriptor parser needs is carried: no hid_device,
 * no input-subsystem mapping, no quirk database, no drivers.
 *
 * HID class request codes (GET_REPORT/IDLE/PROTOCOL, SET_REPORT/IDLE/
 * PROTOCOL) are USB HID 1.11 §7.2 values, not Linux inventions; they are
 * listed here because Linux hid.h does not predefine them either.
 */

#pragma once

#include <stdint.h>

/* Short/long item prefix encoding (HID 1.11 §6.2.2). */
#define HID_ITEM_FORMAT_SHORT 0u
#define HID_ITEM_FORMAT_LONG 1u
#define HID_ITEM_TAG_LONG 15u

#define HID_ITEM_TYPE_MAIN 0u
#define HID_ITEM_TYPE_GLOBAL 1u
#define HID_ITEM_TYPE_LOCAL 2u
#define HID_ITEM_TYPE_RESERVED 3u

/* Main item tags. */
#define HID_MAIN_ITEM_TAG_INPUT 8u
#define HID_MAIN_ITEM_TAG_OUTPUT 9u
#define HID_MAIN_ITEM_TAG_BEGIN_COLLECTION 10u
#define HID_MAIN_ITEM_TAG_FEATURE 11u
#define HID_MAIN_ITEM_TAG_END_COLLECTION 12u

/* Collection types (Collection data byte). */
#define HID_COLLECTION_PHYSICAL 0u
#define HID_COLLECTION_APPLICATION 1u
#define HID_COLLECTION_LOGICAL 2u

/* Global item tags. */
#define HID_GLOBAL_ITEM_TAG_USAGE_PAGE 0u
#define HID_GLOBAL_ITEM_TAG_LOGICAL_MINIMUM 1u
#define HID_GLOBAL_ITEM_TAG_LOGICAL_MAXIMUM 2u
#define HID_GLOBAL_ITEM_TAG_PHYSICAL_MINIMUM 3u
#define HID_GLOBAL_ITEM_TAG_PHYSICAL_MAXIMUM 4u
#define HID_GLOBAL_ITEM_TAG_UNIT_EXPONENT 5u
#define HID_GLOBAL_ITEM_TAG_UNIT 6u
#define HID_GLOBAL_ITEM_TAG_REPORT_SIZE 7u
#define HID_GLOBAL_ITEM_TAG_REPORT_ID 8u
#define HID_GLOBAL_ITEM_TAG_REPORT_COUNT 9u
#define HID_GLOBAL_ITEM_TAG_PUSH 10u
#define HID_GLOBAL_ITEM_TAG_POP 11u

/* Local item tags. */
#define HID_LOCAL_ITEM_TAG_USAGE 0u
#define HID_LOCAL_ITEM_TAG_USAGE_MINIMUM 1u
#define HID_LOCAL_ITEM_TAG_USAGE_MAXIMUM 2u
#define HID_LOCAL_ITEM_TAG_DELIMITER 10u

/* Usage pages of interest (16-bit page number). */
#define HID_UP_GENDESK 0x01u
#define HID_UP_KEYBOARD 0x07u
#define HID_UP_LED 0x08u
#define HID_UP_BUTTON 0x09u

/* Generic-Desktop usages of interest. */
#define HID_GD_POINTER 0x01u
#define HID_GD_MOUSE 0x02u
#define HID_GD_KEYBOARD 0x06u
#define HID_GD_KEYPAD 0x07u

/* HID class-specific request codes (USB HID 1.11 §7.2). */
#define HID_REQ_GET_REPORT 0x01u
#define HID_REQ_GET_IDLE 0x02u
#define HID_REQ_GET_PROTOCOL 0x03u
#define HID_REQ_SET_REPORT 0x09u
#define HID_REQ_SET_IDLE 0x0Au
#define HID_REQ_SET_PROTOCOL 0x0Bu
