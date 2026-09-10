#pragma once
#include <stddef.h>
#include <stdint.h>
#include "acpi.h"

/* P0 Phase A: AP discovery. The BSP boots exactly as before; APs are
 * recorded, never started (see docs/SMP_DESIGN.md).
 *
 * Ownership: smp_build_map() is pure (no HW, no globals) and host
 * tested. smp_discover() runs once pre-scheduler on the BSP; the
 * published map is read-only until Phase C introduces the smp lock. */

#define SMP_MAX_CPUS 64

typedef enum {
    SMP_CPU_ABSENT = 0,
    SMP_CPU_PRESENT = 1,
    SMP_CPU_STARTING = 2,
    SMP_CPU_ONLINE = 3,
    SMP_CPU_OFFLINE = 4
} smp_cpu_state_t;

typedef struct {
    uint32_t apic_id;
    uint8_t enabled;
    uint8_t is_bsp;
    uint8_t x2apic;
    smp_cpu_state_t state;
    uint64_t stack_phys;
} smp_cpu_t;

typedef struct {
    smp_cpu_t cpu[SMP_MAX_CPUS];
    size_t count;
    size_t online;
    uint32_t bsp_apic;
    int bsp_index;
} smp_map_t;

int smp_build_map(const acpi_cpu_info_t *entries, size_t n,
                  uint32_t bsp_apic_id, int bsp_fallback, smp_map_t *out);
int smp_discover(void);
size_t smp_cpu_count(void);
size_t smp_online_count(void);
uint32_t smp_bsp_apic(void);
int smp_bsp_index(void);
const smp_cpu_t *smp_cpu(size_t index);
