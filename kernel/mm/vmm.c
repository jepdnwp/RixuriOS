#include "vmm.h"
#include "pmm.h"
#include <stddef.h>

#define TABLE_ENTRIES 512ULL
#define PAGE_MASK 0x000FFFFFFFFFF000ULL
#define PTE_PS (1ULL << 7)
#define CR0_WP (1ULL << 16)
#define CR4_PAE (1ULL << 5)

static uint64_t early_pml4[512] __attribute__((aligned(4096)));
static uint64_t early_pdpt[512] __attribute__((aligned(4096)));
static uint64_t early_pd[512] __attribute__((aligned(4096)));
static uint64_t current_pml4_phys;

static inline void write_cr3(uint64_t value) { __asm__ volatile ("mov %0,%%cr3" : : "r"(value) : "memory"); }
static inline void invlpg(uint64_t va) { __asm__ volatile ("invlpg (%0)" : : "r"(va) : "memory"); }
static inline uint64_t read_cr0(void) { uint64_t v; __asm__ volatile ("mov %%cr0,%0" : "=r"(v)); return v; }
static inline void write_cr0(uint64_t value) { __asm__ volatile ("mov %0,%%cr0" : : "r"(value) : "memory"); }
static inline uint64_t read_cr4(void) { uint64_t v; __asm__ volatile ("mov %%cr4,%0" : "=r"(v)); return v; }
static inline void write_cr4(uint64_t value) { __asm__ volatile ("mov %0,%%cr4" : : "r"(value) : "memory"); }

static uint64_t *entry_table(uint64_t entry) {
    return (uint64_t *)(uintptr_t)(entry & PAGE_MASK);
}

static int canonical48(uint64_t va) {
    return va < (1ULL << 47) || va >= (UINT64_MAX - (1ULL << 47) + 1ULL);
}

static uint64_t *ensure_table(uint64_t *parent, size_t index, uint64_t flags) {
    uint64_t entry = parent[index];
    if (entry & RIXURI_PTE_PRESENT) return entry_table(entry);
    uint64_t phys = pmm_alloc_page();
    if (!phys) return NULL;
    uint64_t *table = (uint64_t *)(uintptr_t)phys;
    for (size_t i = 0; i < TABLE_ENTRIES; ++i) table[i] = 0;
    parent[index] = phys | RIXURI_PTE_PRESENT | RIXURI_PTE_WRITE |
                    (flags & RIXURI_PTE_USER);
    return table;
}

static uint64_t *split_pd_huge_page(uint64_t *pd, size_t index) {
    uint64_t entry = pd[index];
    if (!(entry & RIXURI_PTE_PRESENT)) return ensure_table(pd, index, 0);
    if (!(entry & PTE_PS)) return entry_table(entry);

    uint64_t phys = pmm_alloc_page();
    if (!phys) return NULL;
    uint64_t *pt = (uint64_t *)(uintptr_t)phys;
    uint64_t base = entry & PAGE_MASK;
    uint64_t leaf_flags = entry & (RIXURI_PTE_WRITE | RIXURI_PTE_USER | RIXURI_PTE_NX);
    for (size_t i = 0; i < TABLE_ENTRIES; ++i)
        pt[i] = (base + (uint64_t)i * 0x1000ULL) | RIXURI_PTE_PRESENT | leaf_flags;
    pd[index] = phys | RIXURI_PTE_PRESENT | RIXURI_PTE_WRITE |
                (entry & RIXURI_PTE_USER);
    return pt;
}

void vmm_early_init(void) {
    for (size_t i = 0; i < TABLE_ENTRIES; ++i) {
        early_pml4[i] = 0;
        early_pdpt[i] = 0;
        early_pd[i] = 0;
    }
    early_pml4[0] = (uint64_t)(uintptr_t)early_pdpt | RIXURI_PTE_PRESENT | RIXURI_PTE_WRITE;
    early_pdpt[0] = (uint64_t)(uintptr_t)early_pd | RIXURI_PTE_PRESENT | RIXURI_PTE_WRITE;
    for (uint64_t i = 0; i < TABLE_ENTRIES; ++i)
        early_pd[i] = i * 0x200000ULL | RIXURI_PTE_PRESENT | RIXURI_PTE_WRITE | PTE_PS;

    current_pml4_phys = (uint64_t)(uintptr_t)early_pml4;
    write_cr3(current_pml4_phys);
    write_cr0(read_cr0() | CR0_WP);
    write_cr4(read_cr4() | CR4_PAE);
}

uint64_t vmm_kernel_pml4(void) { return current_pml4_phys; }

int vmm_map_page(uint64_t va, uint64_t pa, uint64_t flags) {
    if (!canonical48(va) || (va & 0xFFFULL) || (pa & 0xFFFULL) || (pa & ~PAGE_MASK)) return -1;
    uint64_t *pml4 = (uint64_t *)(uintptr_t)current_pml4_phys;
    uint64_t *pdpt = ensure_table(pml4, (va >> 39) & 0x1FFULL, flags); if (!pdpt) return -1;
    uint64_t *pd = ensure_table(pdpt, (va >> 30) & 0x1FFULL, flags); if (!pd) return -1;
    uint64_t *pt = split_pd_huge_page(pd, (va >> 21) & 0x1FFULL); if (!pt) return -1;
    size_t idx = (size_t)((va >> 12) & 0x1FFULL);
    pt[idx] = (pa & PAGE_MASK) | (flags & (RIXURI_PTE_PRESENT | RIXURI_PTE_WRITE | RIXURI_PTE_USER | RIXURI_PTE_NX));
    invlpg(va);
    return 0;
}

void vmm_unmap_page(uint64_t va) {
    if (!canonical48(va) || (va & 0xFFFULL)) return;
    uint64_t *pml4 = (uint64_t *)(uintptr_t)current_pml4_phys;
    uint64_t entry = pml4[(va >> 39) & 0x1FFULL];
    if (!(entry & RIXURI_PTE_PRESENT)) return;
    uint64_t *pdpt = entry_table(entry);
    entry = pdpt[(va >> 30) & 0x1FFULL]; if (!(entry & RIXURI_PTE_PRESENT)) return;
    uint64_t *pd = entry_table(entry);
    entry = pd[(va >> 21) & 0x1FFULL]; if (!(entry & RIXURI_PTE_PRESENT) || (entry & PTE_PS)) return;
    uint64_t *pt = entry_table(entry);
    pt[(va >> 12) & 0x1FFULL] = 0;
    invlpg(va);
}

uint64_t vmm_translate(uint64_t va) {
    if (!canonical48(va)) return 0;
    uint64_t *pml4 = (uint64_t *)(uintptr_t)current_pml4_phys;
    uint64_t entry = pml4[(va >> 39) & 0x1FFULL]; if (!(entry & RIXURI_PTE_PRESENT)) return 0;
    uint64_t *pdpt = entry_table(entry);
    entry = pdpt[(va >> 30) & 0x1FFULL]; if (!(entry & RIXURI_PTE_PRESENT)) return 0;
    uint64_t *pd = entry_table(entry);
    entry = pd[(va >> 21) & 0x1FFULL]; if (!(entry & RIXURI_PTE_PRESENT)) return 0;
    if (entry & PTE_PS) return (entry & PAGE_MASK) + (va & 0x1FFFFFULL);
    uint64_t *pt = entry_table(entry);
    entry = pt[(va >> 12) & 0x1FFULL]; if (!(entry & RIXURI_PTE_PRESENT)) return 0;
    return (entry & PAGE_MASK) + (va & 0xFFFULL);
}
