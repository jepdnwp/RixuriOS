#pragma once

/* Assembler-visible trampoline constants (no C syntax: smp_trampoline.S
 * includes this header through the C preprocessor). */
#define SMP_TRAMP_GDT_OFF 0x200
#define SMP_TRAMP_GDTR_OFF 0x230
#define SMP_TRAMP_DATA_CR3 0x280
#define SMP_TRAMP_DATA_STACK 0x288
#define SMP_TRAMP_DATA_ENTRY 0x290
#define SMP_TRAMP_CRUMB_OFF 0x300
#define SMP_TRAMP_CRUMB_REAL 0xAAu
#define SMP_TRAMP_CRUMB_PROT32 0xBBu
#define SMP_TRAMP_CRUMB_LONG 0xCCu
#define SMP_TRAMP_TEMPLATE_MAX 0x200
#define SMP_TRAMP_STACK_TOP_OFF 0x1000
#define SMP_TRAMP_SEL_CODE32 0x08
#define SMP_TRAMP_SEL_DATA32 0x10
#define SMP_TRAMP_SEL_DATAPAGE 0x18
#define SMP_TRAMP_SEL_CODE64 0x20
#define SMP_TRAMP_SEL_DATA64 0x28

#ifndef __ASSEMBLER__
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
    uint64_t trampoline_phys;
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

/* Phase B: AP startup (docs/SMP_DESIGN.md). Trampoline page layout: the
 * assembled template (code + one patched dword) is copied to a reserved
 * low page; C builds the GDT/GDTR/data area at fixed offsets; the AP
 * stack grows down from page+0x1000. Template must stay under 0x200. */

/* Start recorded APs (INIT-SIPI-SIPI). Returns online count (>=1) or
 * negative when discovery never ran. Never panics: failed APs stay
 * PRESENT and the boot continues (documented DEGRADED). */
int smp_start_aps(void);
/* AP C entry (called once from the trampoline, never returns). */
void ap_entry(void);
/* Volatile state read for the BSP poll loop. */
smp_cpu_state_t smp_cpu_state(size_t index);
/* GDT/GDTR builder over a caller buffer (host-testable). */
int smp_build_gdt(uint8_t *page, uint64_t page_phys);
/* Assembled template size in bytes. */
size_t smp_trampoline_size(void);
/* CPL0-only CR3 read for diagnostics (host-stubbed in unit tests). */
uint64_t smp_read_cr3_hw(void);
/* Copy template to page, build GDT, write data + long-jump patch.
 * Returns 0, -1 on bad input, -2 when CR3 is not reachable in 32-bit
 * mode (kernel PML4 above 4G). Host-testable (buffer-backed). */
int smp_setup_trampoline(uint8_t *page, uint64_t page_phys, uint64_t cr3,
                         uint64_t stack_top, uint64_t entry);
#endif
