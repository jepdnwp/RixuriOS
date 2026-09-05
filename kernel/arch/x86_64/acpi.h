#pragma once
#include <stdint.h>

#define ACPI_MAX_CPUS 64
#define ACPI_MAX_IOAPICS 16

typedef struct { uint8_t id; uint8_t enabled; uint8_t x2apic; uint32_t apic_id; } acpi_cpu_info_t;
typedef struct { uint8_t id; uint32_t address; uint32_t gsi_base; } acpi_ioapic_info_t;

int acpi_init(uint64_t rsdp_phys);
size_t acpi_cpu_count(void);
size_t acpi_ioapic_count(void);
const acpi_cpu_info_t *acpi_cpu(size_t index);
const acpi_ioapic_info_t *acpi_ioapic(size_t index);
uint32_t acpi_irq_gsi(uint8_t irq, uint16_t *flags);
