#include "smp.h"
#include "apic.h"
#include "../../mm/pmm.h"
#include "../../mm/vmm.h"
#include "kernel.h"
#include <stddef.h>
#include <stdint.h>

static smp_map_t smp_map;

/* Phase B spin calibration: PAUSE loops with deliberate overshoot. Exact
 * timing is unknowable without a TSC rate, so every constant errs on the
 * slow side; the ONLINE poll exits early on success. 1M pauses ≈ tens of
 * ms on silicon (protocol minimums: 10 ms INIT, 200 us SIPI gap), more on
 * nested TCG — overshoot is always the safe direction for minimums. */
#define SMP_INIT_DELAY_ITERS 1000000ULL
#define SMP_SIPI_GAP_ITERS 256000ULL
#define SMP_ONLINE_POLL_ROUNDS 60u
#define SMP_ONLINE_POLL_CHUNK 100000ULL
#define SMP_TRAMP_SCAN_LO 0x8000ULL
#define SMP_TRAMP_SCAN_HI 0xA0000ULL

extern char smp_trampoline_start[];
extern char smp_trampoline_end[];
extern char smp_trampoline_patch_longjump[];
extern char smp_trampoline_longmode[];

static void spin_pause(uint64_t iters) {
    for (volatile uint64_t i = 0; i < iters; ++i) __asm__ volatile("pause" ::: "memory");
}

/* CPL0-only; the host unit test overrides it with a stub (userspace
 * cannot read CR3). Weak so the test double links without conflict. */
__attribute__((weak)) uint64_t smp_read_cr3_hw(void) {
    uint64_t value = 0;
    __asm__ volatile("mov %%cr3,%0" : "=r"(value) :: "memory");
    return value;
}

int smp_build_map(const acpi_cpu_info_t *entries, size_t n,
                  uint32_t bsp_apic_id, int bsp_fallback, smp_map_t *out) {
    if (!out) return -1;
    for (size_t i = 0; i < SMP_MAX_CPUS; ++i) {
        out->cpu[i].apic_id = 0;
        out->cpu[i].enabled = 0;
        out->cpu[i].is_bsp = 0;
        out->cpu[i].x2apic = 0;
        out->cpu[i].state = SMP_CPU_ABSENT;
        out->cpu[i].stack_phys = 0;
        out->cpu[i].trampoline_phys = 0;
    }
    out->count = 0;
    out->online = 0;
    out->bsp_apic = bsp_apic_id;
    out->bsp_index = -1;
    if (n && !entries) return -1;
    size_t usable = n < SMP_MAX_CPUS ? n : SMP_MAX_CPUS;
    for (size_t i = 0; i < usable; ++i) {
        size_t duplicate = out->count;
        for (size_t j = 0; j < out->count; ++j)
            if (out->cpu[j].apic_id == entries[i].apic_id) { duplicate = j; break; }
        if (duplicate < out->count) {
            out->cpu[duplicate].enabled |= entries[i].enabled ? 1u : 0u;
            out->cpu[duplicate].x2apic |= entries[i].x2apic ? 1u : 0u;
            continue;
        }
        if (out->count >= SMP_MAX_CPUS) break;
        size_t index = out->count++;
        out->cpu[index].apic_id = entries[i].apic_id;
        out->cpu[index].enabled = entries[i].enabled ? 1u : 0u;
        out->cpu[index].x2apic = entries[i].x2apic ? 1u : 0u;
        out->cpu[index].state = SMP_CPU_PRESENT;
        if (entries[i].apic_id == bsp_apic_id && out->bsp_index < 0) {
            out->cpu[index].is_bsp = 1;
            out->cpu[index].state = SMP_CPU_ONLINE;
            out->bsp_index = (int)index;
            out->online = 1;
        }
    }
    if (out->bsp_index < 0) {
        if (!bsp_fallback) return -1;
        if (usable >= SMP_MAX_CPUS) return -2;
        out->cpu[usable].apic_id = bsp_apic_id;
        out->cpu[usable].enabled = 1;
        out->cpu[usable].is_bsp = 1;
        out->cpu[usable].state = SMP_CPU_ONLINE;
        out->bsp_index = (int)usable;
        out->online = 1;
        out->count = usable + 1;
    }
    return 0;
}

int smp_discover(void) {
    acpi_cpu_info_t list[SMP_MAX_CPUS];
    size_t n = acpi_cpu_count();
    if (n > SMP_MAX_CPUS) n = SMP_MAX_CPUS;
    for (size_t i = 0; i < n; ++i) {
        const acpi_cpu_info_t *entry = acpi_cpu(i);
        if (!entry) return -1;
        list[i] = *entry;
    }
    return smp_build_map(list, n, lapic_id(), 1, &smp_map);
}

size_t smp_cpu_count(void) { return smp_map.count; }
size_t smp_online_count(void) { return smp_map.online; }
uint32_t smp_bsp_apic(void) { return smp_map.bsp_apic; }
int smp_bsp_index(void) { return smp_map.bsp_index; }
const smp_cpu_t *smp_cpu(size_t index) {
    if (index >= SMP_MAX_CPUS || index >= smp_map.count) return 0;
    return &smp_map.cpu[index];
}

smp_cpu_state_t smp_cpu_state(size_t index) {
    if (index >= SMP_MAX_CPUS || index >= smp_map.count) return SMP_CPU_ABSENT;
    return *(volatile smp_cpu_state_t *)&smp_map.cpu[index].state;
}

static void gdt_store(uint8_t *g, uint32_t base, uint32_t limit,
                      uint8_t access, uint8_t flags) {
    g[0] = (uint8_t)(limit & 0xFFu);
    g[1] = (uint8_t)((limit >> 8) & 0xFFu);
    g[2] = (uint8_t)(base & 0xFFu);
    g[3] = (uint8_t)((base >> 8) & 0xFFu);
    g[4] = (uint8_t)((base >> 16) & 0xFFu);
    g[5] = access;
    g[6] = (uint8_t)(((limit >> 16) & 0x0Fu) | (flags & 0xF0u));
    g[7] = (uint8_t)((base >> 24) & 0xFFu);
}

int smp_build_gdt(uint8_t *page, uint64_t page_phys) {
    if (!page || page_phys >= 0x100000000ULL) return -1;
    uint32_t base = (uint32_t)page_phys;
    uint8_t *g = page + SMP_TRAMP_GDT_OFF;
    for (size_t i = 0; i < 48; ++i) g[i] = 0;
    gdt_store(g + 8, base, 0xFFFFFu, 0x9Au, 0xC0u);
    gdt_store(g + 16, 0, 0xFFFFFu, 0x92u, 0xC0u);
    gdt_store(g + 24, base, 0xFFFFFu, 0x92u, 0xC0u);
    gdt_store(g + 32, 0, 0xFFFFFu, 0x9Au, 0x20u);
    gdt_store(g + 40, 0, 0xFFFFFu, 0x92u, 0xC0u);
    uint8_t *gdtr = page + SMP_TRAMP_GDTR_OFF;
    gdtr[0] = 47;
    gdtr[1] = 0;
    uint32_t gdt_base = base + SMP_TRAMP_GDT_OFF;
    gdtr[2] = (uint8_t)(gdt_base & 0xFFu);
    gdtr[3] = (uint8_t)((gdt_base >> 8) & 0xFFu);
    gdtr[4] = (uint8_t)((gdt_base >> 16) & 0xFFu);
    gdtr[5] = (uint8_t)((gdt_base >> 24) & 0xFFu);
    return 0;
}

size_t smp_trampoline_size(void) {
    return (size_t)(smp_trampoline_end - smp_trampoline_start);
}

struct smp_descriptor_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static void smp_capture_descriptor_tables(uint8_t *page) {
    struct smp_descriptor_ptr gdtr, idtr;
    __asm__ volatile("sgdt %0" : "=m"(gdtr) :: "memory");
    __asm__ volatile("sidt %0" : "=m"(idtr) :: "memory");
    for (size_t i = 0; i < sizeof(gdtr); ++i)
        page[SMP_TRAMP_DATA_GDTR + i] = ((const uint8_t *)&gdtr)[i];
    for (size_t i = 0; i < sizeof(idtr); ++i)
        page[SMP_TRAMP_DATA_IDTR + i] = ((const uint8_t *)&idtr)[i];
}

int smp_setup_trampoline(uint8_t *page, uint64_t page_phys, uint64_t cr3,
                         uint64_t stack_top, uint64_t entry) {
    size_t size = smp_trampoline_size();
    if (!page || !page_phys || page_phys >= 0x100000ULL || !entry ||
        !size || size > SMP_TRAMP_TEMPLATE_MAX) return -1;
    if (cr3 >= 0x100000000ULL) return -2;
    if (stack_top <= page_phys || stack_top > page_phys + SMP_TRAMP_STACK_TOP_OFF) return -1;
    const uint8_t *src = (const uint8_t *)smp_trampoline_start;
    for (size_t i = 0; i < size; ++i) page[i] = src[i];
    page[SMP_TRAMP_CRUMB_OFF] = 0;
    if (smp_build_gdt(page, page_phys) != 0) return -1;
    uint64_t patch_off = (uint64_t)(smp_trampoline_patch_longjump - smp_trampoline_start);
    uint64_t long_off = (uint64_t)(smp_trampoline_longmode - smp_trampoline_start);
    if (patch_off + 4 > size || long_off >= size) return -1;
    uint32_t target = (uint32_t)(page_phys + long_off);
    page[patch_off] = (uint8_t)(target & 0xFFu);
    page[patch_off + 1] = (uint8_t)((target >> 8) & 0xFFu);
    page[patch_off + 2] = (uint8_t)((target >> 16) & 0xFFu);
    page[patch_off + 3] = (uint8_t)((target >> 24) & 0xFFu);
    uint8_t *data = page + SMP_TRAMP_DATA_CR3;
    for (size_t i = 0; i < 8; ++i) data[i] = (uint8_t)((cr3 >> (8 * i)) & 0xFFu);
    data = page + SMP_TRAMP_DATA_STACK;
    for (size_t i = 0; i < 8; ++i) data[i] = (uint8_t)((stack_top >> (8 * i)) & 0xFFu);
    data = page + SMP_TRAMP_DATA_ENTRY;
    for (size_t i = 0; i < 8; ++i) data[i] = (uint8_t)((entry >> (8 * i)) & 0xFFu);
    return 0;
}

void ap_entry(void) {
    uint32_t id = lapic_id();
    kernel_log("AP");
    kernel_log_dec(id);
    kernel_log(" E\r\n");
    for (size_t i = 0; i < smp_map.count; ++i) {
        if (smp_map.cpu[i].apic_id == id && !smp_map.cpu[i].is_bsp) {
            smp_map.cpu[i].state = SMP_CPU_ONLINE;
            __asm__ volatile("" ::: "memory");
            break;
        }
    }
    /* Parked: interrupts are never enabled on APs in Phase B, so HLT
     * sleeps until a later phase replaces this loop with the per-CPU
     * idle task. The success path is silent; the BSP prints ordered
     * online lines after observing the flag. */
    for (;;) __asm__ volatile("hlt" ::: "memory");
}

static int trampoline_taken(uint64_t base) {
    for (size_t i = 0; i < smp_map.count; ++i)
        if (smp_map.cpu[i].trampoline_phys == base) return 1;
    return 0;
}

static uint64_t smp_find_trampoline_page(void) {
    /* Low pages are deliberately PMM-unmanaged (pmm_init clears 0..1M),
     * so pmm_is_in_use() reports true and pmm_reserve_page() is a no-op
     * below 1M. Occupancy here means: outside a usable UEFI region, or
     * already recorded in our own map. Failed-AP pages stay recorded and
     * are never reused (4 KiB each, bounded by CPU count). */
    for (uint64_t base = SMP_TRAMP_SCAN_LO;
         base + RIXURI_PAGE_SIZE <= SMP_TRAMP_SCAN_HI;
         base += RIXURI_PAGE_SIZE) {
        uint64_t region_base = 0, region_end = 0;
        uint32_t region_type = 0;
        int usable = 0;
        if (pmm_region_info(base, &region_base, &region_end,
                            &region_type, &usable) != 0) continue;
        if (!usable || base < region_base ||
            base + RIXURI_PAGE_SIZE > region_end) continue;
        if (trampoline_taken(base)) continue;
        return base;
    }
    return 0;
}

int smp_start_aps(void) {
    if (!smp_map.count) return -1;
    if (smp_map.count < 2) return (int)smp_map.online;
    /* The AP fetches code/stack/data through identity-mapped low memory
     * under the tables it will run on. Use the currently active PML4
     * (proven by the BSP itself) rather than assuming the kernel PML4
     * carries a low identity map, and enforce that map explicitly. */
    uint64_t ap_pml4 = vmm_current_pml4();
    if (!ap_pml4) ap_pml4 = vmm_kernel_pml4();
    {
        uint64_t hw_cr3 = smp_read_cr3_hw();
        kernel_log("SMP: cr3hw=");
        kernel_log_hex(hw_cr3);
        kernel_log(" cur=");
        kernel_log_hex(vmm_current_pml4());
        kernel_log(" kern=");
        kernel_log_hex(vmm_kernel_pml4());
        kernel_log("\r\n");
    }
    kernel_log("SMP: AP entry=");
    kernel_log_hex((uint64_t)(uintptr_t)ap_entry);
    kernel_log("\r\n");
    if (!ap_pml4 || ap_pml4 >= 0x100000000ULL) {
        kernel_log("SMP: AP tables above 4G, AP startup deferred\r\n");
        return (int)smp_map.online;
    }
    for (size_t i = 0; i < smp_map.count; ++i) {
        smp_cpu_t *cpu = &smp_map.cpu[i];
        if (cpu->is_bsp || !cpu->enabled ||
            cpu->state != SMP_CPU_PRESENT) continue;
        uint64_t page = smp_find_trampoline_page();
        if (!page) {
            kernel_log("SMP: no low page for AP trampoline\r\n");
            continue;
        }
        kernel_log("SMP: AP ");
        kernel_log_dec(cpu->apic_id);
        kernel_log(" trampoline=");
        kernel_log_hex(page);
        kernel_log("\r\n");
        pmm_reserve_page(page);
        /* Reservation record: pmm_reserve_page() is a no-op below 1M, so
         * the map entry itself is the ownership record (see scan). */
        cpu->trampoline_phys = page;
        uint64_t we0 = 0, we1 = 0, we2 = 0, we3 = 0, wphys = 0, wflags = 0;
        int wrc = vmm_walk_in_pml4(ap_pml4, page, &we0, &we1, &we2, &we3,
                                   &wphys, &wflags);
        if (wrc != 0 || !(wflags & RIXURI_PTE_PRESENT) || wphys != page) {
            /* The trampoline executes from this identity page.  NX here
               makes the fallback path fault as soon as long mode begins. */
            if (vmm_map_page_in_pml4(ap_pml4, page, page,
                    RIXURI_PTE_PRESENT | RIXURI_PTE_WRITE) != 0) {
                kernel_log("SMP: AP identity map failed\r\n");
                continue;
            }
            kernel_log("SMP: AP identity mapped\r\n");
        }
        uint8_t *buffer = vmm_phys_ptr(page);
        if (!buffer) {
            kernel_log("SMP: trampoline page not addressable\r\n");
            continue;
        }
        uint64_t stack_top = page + SMP_TRAMP_STACK_TOP_OFF - 8u;
        if (smp_setup_trampoline(buffer, page, ap_pml4, stack_top,
                                 (uint64_t)(uintptr_t)ap_entry) != 0) {
            kernel_log("SMP: trampoline setup failed\r\n");
            continue;
        }
        smp_capture_descriptor_tables(buffer);
        uint64_t online_ptr = (uint64_t)(uintptr_t)&cpu->state;
        for (size_t b = 0; b < sizeof(online_ptr); ++b)
            buffer[SMP_TRAMP_DATA_ONLINE + b] = (uint8_t)(online_ptr >> (8 * b));
        {
            uint64_t rb = 0;
            for (size_t i = 0; i < 8; ++i)
                rb |= (uint64_t)buffer[SMP_TRAMP_DATA_ENTRY + i] << (8 * i);
            kernel_log("SMP: AP rb=");
            kernel_log_hex(rb);
            kernel_log("\r\n");
        }
        cpu->state = SMP_CPU_STARTING;
        __asm__ volatile("mfence" ::: "memory");
        uint8_t vector = (uint8_t)(page >> 12);
        kernel_log("SMP: AP ");
        kernel_log_dec(cpu->apic_id);
        kernel_log(" INIT vec=");
        kernel_log_hex(vector);
        kernel_log("\r\n");
        spin_pause(SMP_INIT_DELAY_ITERS / 8);
        kernel_log("SMP: AP IPI INIT\r\n");
        if (lapic_send_init(cpu->apic_id, 1) != 0) {
            kernel_log("SMP: INIT assert failed\r\n");
            cpu->state = SMP_CPU_PRESENT;
            continue;
        }
        spin_pause(SMP_INIT_DELAY_ITERS);
        kernel_log("SMP: AP IPI DEASSERT\r\n");
        if (lapic_send_init(cpu->apic_id, 0) != 0) {
            kernel_log("SMP: INIT deassert failed\r\n");
            cpu->state = SMP_CPU_PRESENT;
            continue;
        }
        spin_pause(SMP_SIPI_GAP_ITERS);
        kernel_log("SMP: AP IPI SIPI\r\n");
        if (lapic_send_sipi(cpu->apic_id, vector) != 0) {
            kernel_log("SMP: SIPI send failed\r\n");
            cpu->state = SMP_CPU_PRESENT;
            continue;
        }
        spin_pause(SMP_SIPI_GAP_ITERS);
        if (lapic_send_sipi(cpu->apic_id, vector) != 0) {
            kernel_log("SMP: SIPI resend failed\r\n");
            cpu->state = SMP_CPU_PRESENT;
            continue;
        }
        kernel_log("SMP: AP poll\r\n");
        int online = 0;
        for (uint32_t round = 0; round < SMP_ONLINE_POLL_ROUNDS; ++round) {
            if (smp_cpu_state(i) == SMP_CPU_ONLINE) { online = 1; break; }
            spin_pause(SMP_ONLINE_POLL_CHUNK);
        }
        if (online) {
            smp_map.online++;
        } else {
            /* Crumb via the mapped buffer, never the raw phys address:
             * identity holds on the kernel, but the abstraction (and the
             * host test double) requires the mapped pointer. */
            uint8_t crumb = buffer[SMP_TRAMP_CRUMB_OFF];
            kernel_log("SMP: AP start timeout crumb=");
            kernel_log_hex(crumb);
            kernel_log("\r\n");
            cpu->state = SMP_CPU_PRESENT;
        }
    }
    return (int)smp_map.online;
}
