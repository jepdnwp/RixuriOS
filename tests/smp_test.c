#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include "kernel/arch/x86_64/smp.h"
#include "kernel/arch/x86_64/acpi.h"
#include "kernel/arch/x86_64/tss.h"
#include "kernel/mm/vmm.h"

/* HW stubs: this TU exercises the pure smp_build_map core plus the
 * buffer-backed trampoline setup. IPI/PMM/VMM/log calls are stubbed. */
static size_t stub_cpu_count;
static acpi_cpu_info_t stub_cpus[4];
static uint32_t stub_lapic = 7;
size_t acpi_cpu_count(void) { return stub_cpu_count; }
const acpi_cpu_info_t *acpi_cpu(size_t index) {
    return index < stub_cpu_count ? &stub_cpus[index] : 0;
}
uint32_t lapic_id(void) { return stub_lapic; }
void kernel_log(const char *text) { (void)text; }
void kernel_log_dec(uint64_t value) { (void)value; }
void kernel_log_hex(uint64_t value) { (void)value; }
static int ipi_log[32];
static size_t ipi_count;
int lapic_send_init(uint32_t apic_id, int assert_level) {
    if (ipi_count < 32) ipi_log[ipi_count++] = (int)(apic_id * 10 + (assert_level ? 1 : 2));
    return 0;
}
int lapic_send_sipi(uint32_t apic_id, uint8_t vector) {
    (void)vector;
    if (ipi_count < 32) ipi_log[ipi_count++] = (int)(apic_id * 10 + 3);
    return 0;
}
/* Phase D1 doubles: fixed-IPI send log with optional synchronous AP
 * receipt synthesis (drives the real x86_ipi_dispatch path), EOI counter,
 * and a recording smp_flush_one override (host ring 3 cannot invlpg). */
static int fixed_ipi_log[16];
static size_t fixed_ipi_count;
static int fixed_ipi_rc;
static int fixed_ipi_synth;
static int eoi_count;
void lapic_eoi(void) { eoi_count++; }
static int ap_enable_count;
void lapic_ap_enable(void) { ap_enable_count++; }
static uint64_t flushed[8];
static size_t flushed_count;
void smp_flush_one(uint64_t va) {
    if (flushed_count < 8) flushed[flushed_count++] = va;
}
int lapic_send_ipi(uint32_t apic_id, uint8_t vector) {
    if (fixed_ipi_count < 16)
        fixed_ipi_log[fixed_ipi_count++] = (int)(apic_id * 256u + vector);
    if (fixed_ipi_synth && fixed_ipi_rc == 0) {
        stub_lapic = apic_id;
        struct { uint64_t vector, error, rip, cs, rflags, rsp, ss; } fr =
            { vector, 0, 0, 0, 0, 0, 0 };
        x86_ipi_dispatch(&fr);
    }
    return fixed_ipi_rc;
}
/* Test-only const bend: the API hands out const handles, but the harness
 * must arrange ONLINE states that real APs would publish. Production code
 * never casts this away. */
static void force_online(size_t i) {
    smp_cpu_t *c = (smp_cpu_t *)smp_cpu(i);
    assert(c != 0);
    c->state = SMP_CPU_ONLINE;
}
static uint64_t stub_pml4 = 0x200000ULL;
uint64_t vmm_kernel_pml4(void) { return stub_pml4; }
uint64_t vmm_current_pml4(void) { return stub_pml4; }
uint64_t smp_read_cr3_hw(void) { return stub_pml4; }
static int walk_result = 1;
static int map_calls;
static uint64_t map_va, map_pa, map_flags;
int vmm_walk_in_pml4(uint64_t pml4, uint64_t va, uint64_t *e0, uint64_t *e1,
                     uint64_t *e2, uint64_t *e3, uint64_t *phys, uint64_t *flags) {
    (void)pml4; (void)va;
    if (e0) *e0 = 0;
    if (e1) *e1 = 0;
    if (e2) *e2 = 0;
    if (e3) *e3 = 0;
    if (phys) *phys = 0;
    if (flags) *flags = 0;
    return walk_result;
}
int vmm_map_page_in_pml4(uint64_t pml4, uint64_t va, uint64_t pa, uint64_t flags) {
    (void)pml4;
    ++map_calls;
    map_va = va; map_pa = pa; map_flags = flags;
    return 0;
}
static uint8_t fake_pages[16][4096];
static uint64_t fake_phys[16];
static size_t fake_count;
void *vmm_phys_ptr(uint64_t pa) {
    for (size_t i = 0; i < fake_count; ++i)
        if (fake_phys[i] == pa) return fake_pages[i];
    if (fake_count < 16 && pa != 0 && !(pa & 0xFFFu)) {
        fake_phys[fake_count] = pa;
        return fake_pages[fake_count++];
    }
    return 0;
}
/* Backing store for the per-CPU stacks (4 pages each) plus the Phase-C2
 * per-AP TSS page and DF-stack page (1 each) smp_start_aps allocates. */
static uint8_t cpu_arena[40][4096] __attribute__((aligned(4096)));
static size_t cpu_arena_next;
uint64_t pmm_alloc_pages(size_t count) {
    if (!count || cpu_arena_next + count > 40) return 0;
    uint64_t base = (uint64_t)(uintptr_t)&cpu_arena[cpu_arena_next];
    cpu_arena_next += count;
    return base;
}
static uint64_t reserved_pages[8];
static size_t reserved_count;
int pmm_region_info(uint64_t pa, uint64_t *base, uint64_t *end,
                    uint32_t *type, int *usable) {
    if (pa < 0x8000ULL || pa >= 0xA0000ULL) return -1;
    if (base) *base = 0x8000ULL;
    if (end) *end = 0xA0000ULL;
    if (type) *type = 1;
    if (usable) *usable = 1;
    return 0;
}
int pmm_is_in_use(uint64_t pa) { (void)pa; return 0; }
int pmm_is_reserved(uint64_t pa) {
    for (size_t i = 0; i < reserved_count; ++i)
        if (reserved_pages[i] == pa) return 1;
    return 0;
}
void pmm_reserve_page(uint64_t pa) {
    if (reserved_count < 8) reserved_pages[reserved_count++] = pa;
}

static acpi_cpu_info_t entry(uint32_t apic, uint8_t enabled, uint8_t x2apic) {
    acpi_cpu_info_t e = {0, enabled, x2apic, apic};
    return e;
}

int main(void) {
    smp_map_t map;
    acpi_cpu_info_t four[4] = {entry(0, 1, 0), entry(1, 1, 0),
                               entry(2, 1, 0), entry(3, 1, 0)};
    assert(smp_build_map(four, 4, 0, 1, &map) == 0);
    assert(map.count == 4 && map.online == 1 && map.bsp_index == 0);
    assert(map.cpu[0].is_bsp && map.cpu[0].state == SMP_CPU_ONLINE);
    assert(map.cpu[3].state == SMP_CPU_PRESENT && !map.cpu[3].is_bsp);

    assert(smp_build_map(four, 4, 2, 1, &map) == 0 && map.bsp_index == 2);
    assert(map.online == 1 && map.cpu[2].state == SMP_CPU_ONLINE);

    acpi_cpu_info_t mixed[3] = {entry(0, 0, 0), entry(1, 1, 0), entry(0x100, 1, 1)};
    assert(smp_build_map(mixed, 3, 0x100, 1, &map) == 0);
    assert(map.count == 3 && map.bsp_index == 2 && map.online == 1);
    assert(map.cpu[0].state == SMP_CPU_PRESENT && !map.cpu[0].enabled);

    assert(smp_build_map(0, 0, 9, 1, &map) == 0);
    assert(map.count == 1 && map.online == 1 && map.bsp_index == 0);
    assert(map.cpu[0].apic_id == 9 && map.cpu[0].is_bsp);

    assert(smp_build_map(0, 0, 9, 0, &map) != 0);
    assert(smp_build_map(four, 4, 0, 1, 0) != 0);
    assert(smp_build_map(0, 3, 0, 1, &map) != 0);

    acpi_cpu_info_t many[70];
    for (size_t i = 0; i < 70; ++i) many[i] = entry((uint32_t)i, 1, 0);
    assert(smp_build_map(many, 70, 0, 1, &map) == 0 && map.count == 64);
    assert(smp_build_map(many, 70, 9999, 1, &map) != 0);

    stub_cpu_count = 0;
    assert(smp_discover() == 0);
    assert(smp_cpu_count() == 1 && smp_online_count() == 1);
    assert(smp_bsp_apic() == 7 && smp_bsp_index() == 0);
    assert(smp_cpu(0) && smp_cpu(0)->is_bsp && smp_cpu(1) == 0);

    extern char smp_trampoline_start[];
    extern char smp_trampoline_end[];
    extern char smp_trampoline_patch_longjump[];
    extern char smp_trampoline_longmode[];
    size_t template_size = smp_trampoline_size();
    assert(template_size > 32 && template_size <= SMP_TRAMP_TEMPLATE_MAX);
    assert((size_t)(smp_trampoline_end - smp_trampoline_start) == template_size);
    size_t patch_off = (size_t)(smp_trampoline_patch_longjump - smp_trampoline_start);
    size_t long_off = (size_t)(smp_trampoline_longmode - smp_trampoline_start);
    assert(patch_off + 4 <= template_size && long_off < template_size);
    const uint8_t *templ0 = (const uint8_t *)smp_trampoline_start;
    assert(templ0[patch_off] == 0x78 && templ0[patch_off + 1] == 0x56 &&
           templ0[patch_off + 2] == 0x34 && templ0[patch_off + 3] == 0x12);
    /* Structural proof that the patch site is the ljmp offset field:
     * byte before it is EA (far jump), 4 bytes after is the code selector,
     * and the longmode target sits inside the template. This pins the
     * ". - 6" fix: ". - 4" would corrupt the selector. */
    assert(template_size > 8 && templ0[patch_off - 1] == 0xEA);
    assert(templ0[patch_off + 4] == SMP_TRAMP_SEL_CODE64 &&
           templ0[patch_off + 5] == 0x00);
    assert(long_off > patch_off + 4);
    assert(SMP_TRAMP_CRUMB_OFF > SMP_TRAMP_DATA_ENTRY + 8);
    assert(SMP_TRAMP_CRUMB_OFF < SMP_TRAMP_STACK_TOP_OFF);

    static uint8_t page[4096];
    static uint8_t vpage[4096];
    for (size_t i = 0; i < sizeof(page); ++i) page[i] = 0;
    assert(smp_build_gdt(page, 0x8000ULL) == 0);
    assert(smp_build_gdt(0, 0x8000ULL) != 0);
    assert(smp_build_gdt(page, 0x100000000ULL) != 0);
    uint8_t *gdt = page + SMP_TRAMP_GDT_OFF;
    for (size_t i = 0; i < 8; ++i) assert(gdt[i] == 0);
    assert(gdt[8 + 5] == 0x9Au && (gdt[8 + 6] & 0xF0u) == 0xC0u);
    assert(gdt[24 + 2] == 0x00 && gdt[24 + 3] == 0x80 && gdt[24 + 7] == 0x00);
    assert(gdt[32 + 5] == 0x9Au && (gdt[32 + 6] & 0xF0u) == 0x20u);
    uint8_t *gdtr = page + SMP_TRAMP_GDTR_OFF;
    assert(gdtr[0] == 47 && gdtr[1] == 0);
    uint32_t gdtr_base = (uint32_t)gdtr[2] | ((uint32_t)gdtr[3] << 8) |
                         ((uint32_t)gdtr[4] << 16) | ((uint32_t)gdtr[5] << 24);
    assert(gdtr_base == 0x8000u + SMP_TRAMP_GDT_OFF);

    for (size_t i = 0; i < sizeof(page); ++i) page[i] = 0;
    uint64_t stack_top = 0x8000ULL + SMP_TRAMP_STACK_TOP_OFF - 8u;
    assert(smp_setup_trampoline(page, 0x8000ULL, 0x100000ULL, stack_top, 0x400000ULL) == 0);
    uint32_t patched = (uint32_t)page[patch_off] | ((uint32_t)page[patch_off + 1] << 8) |
                       ((uint32_t)page[patch_off + 2] << 16) | ((uint32_t)page[patch_off + 3] << 24);
    assert(patched == 0x8000u + (uint32_t)long_off);
    uint64_t stored = 0;
    for (size_t i = 0; i < 8; ++i) stored |= (uint64_t)page[SMP_TRAMP_DATA_CR3 + i] << (8 * i);
    assert(stored == 0x100000ULL);
    stored = 0;
    for (size_t i = 0; i < 8; ++i) stored |= (uint64_t)page[SMP_TRAMP_DATA_ENTRY + i] << (8 * i);
    assert(stored == 0x400000ULL);
    assert(page[SMP_TRAMP_CRUMB_OFF] == 0);
    assert(smp_setup_trampoline(0, 0x8000ULL, 0x100000ULL, stack_top, 0x400000ULL) != 0);
    assert(smp_setup_trampoline(page, 0x100000ULL, 0x100000ULL, stack_top, 0x400000ULL) != 0);
    assert(smp_setup_trampoline(page, 0x8000ULL, 0x100000000ULL, stack_top, 0x400000ULL) == -2);
    /* Phase C1 contract: stack_top is a per-CPU kernel stack top, only
     * nonzero + 8-aligned is required (no longer confined to the page). */
    assert(smp_setup_trampoline(page, 0x8000ULL, 0x100000ULL, 0, 0x400000ULL) != 0);
    assert(smp_setup_trampoline(page, 0x8000ULL, 0x100000ULL, 0x7001ULL, 0x400000ULL) != 0);
    assert(smp_setup_trampoline(page, 0x8000ULL, 0x100000ULL, 0x1C0000ULL - 8u, 0x400000ULL) == 0);
    assert(smp_setup_trampoline(page, 0x8000ULL, 0x100000ULL, 0x7000ULL, 0) != 0);
    assert(smp_setup_trampoline(page, 0x8000ULL, 0x100000ULL, stack_top, 0) != 0);

    stub_cpus[0] = entry(0, 1, 0);
    stub_cpus[1] = entry(1, 1, 0);
    stub_cpus[2] = entry(2, 1, 0);
    stub_cpus[3] = entry(3, 1, 0);
    stub_cpu_count = 4;
    stub_lapic = 0;
    ipi_count = 0;
    reserved_count = 0;
    walk_result = 1;
    map_calls = 0;
    assert(smp_discover() == 0 && smp_cpu_count() == 4);
    /* Phase C1: LAPIC-ID to map-index lookup over the discovered map. */
    stub_lapic = 0;
    assert(smp_cpu_id() == 0);
    stub_lapic = 2;
    assert(smp_cpu_id() == 2);
    stub_lapic = 9;
    assert(smp_cpu_id() == -1);
    stub_lapic = 0;
    assert(smp_start_aps() == 1);
    assert(map_calls == 3 && map_va == 0xA000ULL && map_pa == 0xA000ULL);
    assert((map_flags & RIXURI_PTE_PRESENT) != 0);
    assert((map_flags & RIXURI_PTE_WRITE) != 0);
    /* The trampoline executes from this identity page (real mode has no NX;
     * NX here would fault the long-mode fallback). Production smp.c maps it
     * PRESENT|WRITE on purpose; the harness pins that contract. */
    assert((map_flags & RIXURI_PTE_NX) == 0);
    assert((map_flags & RIXURI_PTE_USER) == 0);
    assert(smp_online_count() == 1);
    assert(smp_cpu(1)->state == SMP_CPU_PRESENT && smp_cpu(2)->state == SMP_CPU_PRESENT);
    assert(smp_cpu(1)->trampoline_phys == 0x8000ULL);
    assert(smp_cpu(2)->trampoline_phys == 0x9000ULL);
    assert(smp_cpu(3)->trampoline_phys == 0xA000ULL);
    /* Phase C1: each started AP owns a recorded 16 KiB stack. */
    assert(smp_cpu(1)->stack_phys != 0);
    assert(smp_cpu(2)->stack_phys != 0 && smp_cpu(2)->stack_phys != smp_cpu(1)->stack_phys);
    assert(smp_cpu(3)->stack_phys != 0 && smp_cpu(3)->stack_phys != smp_cpu(2)->stack_phys);
    /* Phase C2: each started AP owns a recorded TSS page + DF stack. */
    assert(smp_cpu(1)->tss_page_phys != 0);
    assert(smp_cpu(2)->tss_page_phys != 0 && smp_cpu(2)->tss_page_phys != smp_cpu(1)->tss_page_phys);
    assert(smp_cpu(3)->tss_page_phys != 0 && smp_cpu(3)->tss_page_phys != smp_cpu(2)->tss_page_phys);
    assert(smp_cpu(1)->df_stack_phys != 0);
    assert(smp_cpu(2)->df_stack_phys != 0 && smp_cpu(2)->df_stack_phys != smp_cpu(1)->df_stack_phys);
    assert(smp_cpu(3)->df_stack_phys != 0 && smp_cpu(3)->df_stack_phys != smp_cpu(2)->df_stack_phys);
    for (size_t ap = 1; ap <= 3; ++ap) {
        uint8_t *tmem = vmm_phys_ptr(smp_cpu(ap)->tss_page_phys);
        assert(tmem != 0);
        uint64_t e1 = 0, e5lo = 0;
        for (size_t b = 0; b < 8; ++b) {
            e1 |= (uint64_t)tmem[8 + b] << (8 * b);
            e5lo |= (uint64_t)tmem[5 * 8 + b] << (8 * b);
        }
        assert(e1 == 0x00AF9A000000FFFFULL);
        assert(((e5lo >> 40) & 0xFFULL) == 0x89ULL);
        /* TSS content: rsp0 = AP stack top, ist[0] = DF top, no bitmap. */
        uint8_t *ts = tmem + GDT_CPU_TSS_OFF;
        uint64_t rsp0 = 0, ist0 = 0;
        for (size_t b = 0; b < 8; ++b) {
            rsp0 |= (uint64_t)ts[4 + b] << (8 * b);
            ist0 |= (uint64_t)ts[36 + b] << (8 * b);
        }
        assert(rsp0 == smp_cpu(ap)->stack_phys + 4u * 4096u - 8u);
        /* ist[0] is a VA (physmap of the DF page), not the phys value. */
        assert(ist0 == (uint64_t)(uintptr_t)(vmm_phys_ptr(smp_cpu(ap)->df_stack_phys) + 4096u));
        assert(ts[102] == (uint8_t)sizeof(x86_tss_t) && ts[103] == 0);
        /* Trampoline GDTR slot targets this AP's copy (limit 55). */
        uint8_t *tbuf = vmm_phys_ptr(smp_cpu(ap)->trampoline_phys);
        assert(tbuf != 0);
        uint16_t glim = (uint16_t)tbuf[SMP_TRAMP_DATA_GDTR] |
                        ((uint16_t)tbuf[SMP_TRAMP_DATA_GDTR + 1] << 8);
        uint64_t gbase = 0;
        for (size_t b = 0; b < 8; ++b)
            gbase |= (uint64_t)tbuf[SMP_TRAMP_DATA_GDTR + 2 + b] << (8 * b);
        assert(glim == GDT_CPU_COPY_ENTRIES * 8u - 1u);
        assert(gbase == (uint64_t)(uintptr_t)tmem);
    }
    /* Routing through the strong tss_cpu_index override: an AP LAPIC ID
     * resolves to its registered TSS, unknown IDs stay off it. */
    stub_lapic = 1;
    assert(tss_current() ==
           (const x86_tss_t *)(vmm_phys_ptr(smp_cpu(1)->tss_page_phys) + GDT_CPU_TSS_OFF));
    stub_lapic = 9;
    assert(tss_current() !=
           (const x86_tss_t *)(vmm_phys_ptr(smp_cpu(1)->tss_page_phys) + GDT_CPU_TSS_OFF));
    stub_lapic = 0;
    for (size_t ap = 0; ap < 3; ++ap) {
        uint64_t phys = 0x8000ULL + ap * 0x1000ULL;
        uint8_t *written = vmm_phys_ptr(phys);
        assert(written != 0);
        size_t tsize = smp_trampoline_size();
        const uint8_t *templ = (const uint8_t *)smp_trampoline_start;
        size_t poff = (size_t)(smp_trampoline_patch_longjump - smp_trampoline_start);
        size_t loff = (size_t)(smp_trampoline_longmode - smp_trampoline_start);
        for (size_t i = 0; i < tsize; ++i) {
            if (i >= poff && i < poff + 4) continue;
            assert(written[i] == templ[i]);
        }
        uint32_t patched2 = (uint32_t)written[poff] | ((uint32_t)written[poff + 1] << 8) |
                            ((uint32_t)written[poff + 2] << 16) | ((uint32_t)written[poff + 3] << 24);
        assert(patched2 == (uint32_t)(phys + loff));
    }
    assert(ipi_count == 12);
    for (size_t ap = 0; ap < 3; ++ap) {
        uint32_t id = (uint32_t)(ap + 1);
        assert(ipi_log[ap * 4] == (int)(id * 10 + 1));
        assert(ipi_log[ap * 4 + 1] == (int)(id * 10 + 2));
        assert(ipi_log[ap * 4 + 2] == (int)(id * 10 + 3));
        assert(ipi_log[ap * 4 + 3] == (int)(id * 10 + 3));
    }
    /* Phase D2: trampoline checksum contract (offsets + guards). The
     * reference sum below replicates the documented wire format
     * ([CSUM_BEGIN, CSUM_END) LE words vs the CSUM slot); production and
     * the asm gate must compute the same value (QEMU proves the asm side
     * by reaching ONLINE). */
    for (size_t i = 0; i < sizeof(vpage); ++i) vpage[i] = 0;
    assert(smp_verify_trampoline(0) != 0);
    assert(smp_verify_trampoline(vpage) != 0);
    for (size_t i = 0; i < 8; ++i) vpage[SMP_TRAMP_DATA_CSUM + i] = 0xA5;
    assert(smp_verify_trampoline(vpage) != 0);
    for (size_t i = SMP_TRAMP_CSUM_BEGIN; i < SMP_TRAMP_CSUM_END; ++i)
        vpage[i] = (uint8_t)(i * 3u + 1u);
    {
        uint64_t s = 0;
        for (uint64_t off = SMP_TRAMP_CSUM_BEGIN; off + 8 <= SMP_TRAMP_CSUM_END; off += 8) {
            uint64_t w = 0;
            for (size_t b = 0; b < 8; ++b) w |= (uint64_t)vpage[off + b] << (8 * b);
            s += w;
        }
        for (size_t b = 0; b < 8; ++b) vpage[SMP_TRAMP_DATA_CSUM + b] = (uint8_t)(s >> (8 * b));
    }
    assert(smp_verify_trampoline(vpage) == 0);
    vpage[SMP_TRAMP_DATA_IDTR + 8] ^= 0xFFu;
    assert(smp_verify_trampoline(vpage) != 0);
    /* Phase D1: bad ping targets never send (BSP index, out of range,
     * PRESENT-but-offline AP). */
    assert(smp_ping(99) == -2);
    assert(smp_ping(0) == -2);
    assert(smp_ping(1) == -2);
    assert(fixed_ipi_count == 0);
    /* Ping timeout terminates bounded with no ack (send succeeds). */
    force_online(1);
    stub_lapic = 1;
    fixed_ipi_rc = 0;
    fixed_ipi_synth = 0;
    assert(smp_ping(1) == -1);
    assert(fixed_ipi_count == 1);
    assert(fixed_ipi_log[0] == (int)(1u * 256u + SMP_IPI_PING));
    /* Ping success through the real send->dispatch->ack->poll path. */
    force_online(2);
    stub_lapic = 2;
    fixed_ipi_synth = 1;
    {
        size_t before = fixed_ipi_count;
        int eoi_before = eoi_count;
        assert(smp_ping(2) == 0);
        assert(fixed_ipi_count == before + 1);
        assert(eoi_count == eoi_before + 1);
    }
    /* Direct dispatch contract: EOI always, unknown/NULL frames safe. */
    {
        struct { uint64_t vector, error, rip, cs, rflags, rsp, ss; } fr =
            { SMP_IPI_PING, 0, 0, 0, 0, 0, 0 };
        int eoi_before = eoi_count;
        stub_lapic = 2;
        x86_ipi_dispatch(&fr);
        fr.vector = 0xE2u;
        x86_ipi_dispatch(&fr);
        x86_ipi_dispatch(0);
        assert(eoi_count == eoi_before + 3);
    }
    /* Shootdown negatives: no IPI, no flush. */
    flushed_count = 0;
    assert(smp_shootdown(0) == -2);
    assert(smp_shootdown(1ULL << 47) == -2);
    assert(fixed_ipi_count == 2);
    assert(flushed_count == 0);
    /* Shootdown success across two ONLINE APs (seq logic exercised:
     * one local flush plus one synthesized flush per AP). */
    fixed_ipi_synth = 1;
    fixed_ipi_rc = 0;
    assert(smp_shootdown(0x5000u) == 0);
    assert(flushed_count == 3);
    assert(flushed[0] == 0x5000u && flushed[1] == 0x5000u && flushed[2] == 0x5000u);
    assert(fixed_ipi_count == 4);
    /* Shootdown send failure fails closed. */
    fixed_ipi_rc = -1;
    assert(smp_shootdown(0x6000u) == -1);
    /* UP fast path: single-CPU map flushes locally with no IPI. */
    stub_cpu_count = 1;
    stub_cpus[0] = entry(0, 1, 0);
    stub_lapic = 0;
    assert(smp_discover() == 0 && smp_cpu_count() == 1);
    flushed_count = 0;
    {
        size_t ipi_before = fixed_ipi_count;
        assert(smp_shootdown(0x2000u) == 0);
        assert(fixed_ipi_count == ipi_before);
        assert(flushed_count == 1 && flushed[0] == 0x2000u);
    }
    return 0;
}
