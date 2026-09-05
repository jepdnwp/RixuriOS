#pragma once
#include <stdint.h>
int ioapic_init(void);
int ioapic_route_irq(unsigned irq, uint8_t vector, uint8_t apic_id);
void ioapic_mask_irq(unsigned irq);
void ioapic_unmask_irq(unsigned irq);
