#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include "kernel/arch/x86_64/tss.h"

/* Host harness for the pure Phase-C2 GDT-copy builder plus the TSS
 * registry negatives. Only gdt_build_cpu_copy / tss_register_cpu /
 * tss_current are exercised here (never gdt_init/tss_init: lgdt/ltr are
 * privileged). The weak tss_cpu_index default (-1, no smp.c linked)
 * keeps routing on the static TSS. */

int main(void) {
    uint64_t g[GDT_CPU_COPY_ENTRIES];
    for (size_t i = 0; i < GDT_CPU_COPY_ENTRIES; ++i) g[i] = 0xA5A5A5A5A5A5A5A5ULL;
    assert(gdt_build_cpu_copy(0, 0x1000ULL, 103) != 0);
    assert(gdt_build_cpu_copy(g, 0, 103) != 0);
    assert(gdt_build_cpu_copy(g, 0xFFFF800000200040ULL, 103) == 0);
    /* Template entries identical to the BSP GDT. */
    assert(g[0] == 0);
    assert(g[1] == 0x00AF9A000000FFFFULL);
    assert(g[2] == 0x00CF92000000FFFFULL);
    assert(g[3] == 0x00AFFA000000FFFFULL);
    assert(g[4] == 0x00CFF2000000FFFFULL);
    /* TSS descriptor: base round-trips, type 0x89 present, limit 103. */
    {
        uint64_t lo = g[5], hi = g[6];
        uint64_t base = ((lo >> 16) & 0xFFFFFFULL) |
                        (((lo >> 56) & 0xFFULL) << 24) | (hi << 32);
        assert(base == 0xFFFF800000200040ULL);
        assert(((lo >> 40) & 0xFFULL) == 0x89ULL);
        uint32_t lim = (uint32_t)(lo & 0xFFFFULL) |
                       ((uint32_t)((lo >> 48) & 0xFULL) << 16);
        assert(lim == 103);
    }
    /* Registry negatives; routing stays on the static TSS here. */
    static x86_tss_t fake;
    fake.rsp0 = 0;
    assert(tss_register_cpu(-1, &fake) != 0);
    assert(tss_register_cpu(TSS_MAX_CPUS, &fake) != 0);
    assert(tss_register_cpu(0, 0) != 0);
    assert(tss_register_cpu(3, &fake) == 0);
    tss_set_rsp0(0x1234ULL);
    const x86_tss_t *cur = tss_current();
    assert(cur != 0 && cur != &fake && cur->rsp0 == 0x1234ULL);
    return 0;
}
