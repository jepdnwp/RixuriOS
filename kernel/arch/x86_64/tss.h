#pragma once
#include <stdint.h>
typedef struct __attribute__((packed)){uint32_t reserved0;uint64_t rsp0,rsp1,rsp2;uint64_t reserved1;uint64_t ist[7];uint64_t reserved2;uint16_t reserved3;uint16_t iomap_base;} x86_tss_t;
void tss_init(void);
void tss_set_rsp0(uint64_t stack_top);
const x86_tss_t *tss_current(void);

/* Phase C2: per-CPU TSS registry. AP GDT copies keep the 7-entry layout
 * (selector 0x28 still the TSS everywhere); each copy's TSS descriptor
 * points at that CPU's own x86_tss_t. TSS_MAX_CPUS must cover
 * SMP_MAX_CPUS (asserted in smp.c, not here: tss.h stays free of smp.h
 * so gdt.c gains no new include edges). */
#define TSS_MAX_CPUS 64
#define GDT_CPU_COPY_ENTRIES 7
#define GDT_CPU_TSS_OFF 64
/* Pure 7-entry GDT copy builder (host-testable): entries 0..4 are the
 * kernel template, entry 5..6 the TSS descriptor. 0 ok, -1 bad input. */
int gdt_build_cpu_copy(uint64_t out[GDT_CPU_COPY_ENTRIES], uint64_t tss_base,
                       uint32_t tss_limit);
/* Register an AP's TSS (called once per started AP at bringup). 0 ok,
 * -1 bad index/pointer. The BSP never registers: it keeps the static TSS. */
int tss_register_cpu(int idx, x86_tss_t *tss_ptr);
/* Current-CPU index for TSS routing. Weak default (-1) in gdt.c; smp.c
 * provides the strong override (smp_cpu_id). Same weak-stub pattern as
 * smp_read_cr3_hw / smp_flush_one. */
int tss_cpu_index(void);
