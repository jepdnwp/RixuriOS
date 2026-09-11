#pragma once

#include <stdint.h>

#define RIXURI_LAPIC_SPURIOUS_VECTOR 0xFFu

int lapic_init(void);
uint32_t lapic_id(void);
void lapic_eoi(void);
void lapic_write(uint32_t offset, uint32_t value);
uint32_t lapic_read(uint32_t offset);
void lapic_enable_pic_extint(void);

/* P0 Phase B: inter-processor interrupts (xAPIC MMIO form). All three
 * poll the delivery-status bit with a bounded iteration count and
 * return 0 on send, negative on timeout. No sleep, no scheduler use. */
#define APIC_REG_ICR_LOW 0x300u
#define APIC_REG_ICR_HIGH 0x310u
#define APIC_ICR_DELIVERY_STATUS (1u << 12)
#define APIC_DELIVERY_INIT 0x500u
#define APIC_DELIVERY_SIPI 0x600u
#define APIC_LEVEL_ASSERT (1u << 14)
#define APIC_TRIGGER_LEVEL (1u << 15)
int lapic_send_init(uint32_t apic_id, int assert_level);
int lapic_send_sipi(uint32_t apic_id, uint8_t vector);
/* Phase D: fixed-delivery IPI to one LAPIC (physical destination). Same
 * bounded delivery-status discipline as INIT/SIPI. No sleep, no scheduler. */
int lapic_send_ipi(uint32_t apic_id, uint8_t vector);
/* Enable a secondary CPU's LAPIC for fixed IPIs (SVR + EOI flush).
 * Used by ap_entry; the BSP path is lapic_init(). */
void lapic_ap_enable(void);
