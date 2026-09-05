#include "vmm.h"
#include "pmm.h"
#include <stddef.h>
#include <stdint.h>

#define TABLE_ENTRIES 512ULL
#define PTE_ADDR_MASK 0x000FFFFFFFFFF000ULL
#define PTE_PS (1ULL << 7)
#define PAGE_2M (2ULL * 1024ULL * 1024ULL)
#define VA_BITS 48ULL

static uint64_t early_pml4[512] __attribute__((aligned(4096)));
static uint64_t early_pdpt[512] __attribute__((aligned(4096)));
static uint64_t early_pd[512] __attribute__((aligned(4096)));

static inline uint64_t read_cr3(void) {
    uint64_t value;
    __asm__ volatile("mov %%cr3,%0" : "=r"(value));
    return value;
}

static inline void write_cr3(uint64_t value) {
    __asm__ volatile("mov %0,%%cr3" : : "r"(value) : "memory");
}

static inline void invlpg(uint64_t va) {
    __asm__ volatile("invlpg (%0)" : : "r"(va) : "memory");
}

static int canonical(uint64_t va) {
    uint64_t top = va >> VA_BITS;
    return top == 0 || top == 0xffffULL;
}

static uint64_t *table_for(uint64_t entry) {
    return (uint64_t *)(uintptr_t)(entry & PTE_ADDR_MASK);
}

static uint64_t *alloc_table(void) {
    uint64_t phys = pmm_alloc_page();
    if (!phys || phys >= 0x40000000ULL) return NULL; /* early identity map ceiling */
    uint64_t *table = (uint64_t *)(uintptr_t)phys;
    for (size_t i = 0; i < TABLE_ENTRIES; ++i) table[i] = 0;
    return table;
}

void vmm_early_init(void) {
    for (size_t i = 0; i < TABLE_ENTRIES; ++i) {
        early_pml4[i] = 0;
        early_pdpt[i] = 0;
        early_pd[i] = 0;
    }

    early_pml4[0] = (uint64_t)(uintptr_t)early_pdpt | RIXURI_PTE_PRESENT | RIXURI_PTE_WRITE;
    early_pdpt[0] = (uint64_t)(uintptr_t)early_pd | RIXURI_PTE_PRESENT | RIXURI_PTE_WRITE;

    /* Temporary identity map: first 1 GiB with 2 MiB pages. */
    for (uint64_t i = 0; i < TABLE_ENTRIES; ++i)
        early_pd[i] = (i * PAGE_2M) | RIXURI_PTE_PRESENT | RIXURI_PTE_WRITE | PTE_PS;

    write_cr3((uint64_t)(uintptr_t)early_pml4);
}

uint64_t vmm_kernel_pml4(void) { return (uint64_t)(uintptr_t)early_pml4; }

/* Convert a 2 MiB PDE into a page table before installing a 4 KiB mapping. */
static uint64_t *split_2m(uint64_t *pd, size_t index) {
    uint64_t old = pd[index];
    if (!(old & PTE_PS)) return table_for(old);

    uint64_t phys = pmm_alloc_page();
    if (!phys || phys >= 0x40000000ULL) return NULL;
    uint64_t *pt = (uint64_t *)(uintptr_t)phys;
    uint64_t base = old & PTE_ADDR_MASK;
    uint64_t inherited = old & (RIXURI_PTE_PRESENT | RIXURI_PTE_WRITE | RIXURI_PTE_USER | RIXURI_PTE_NX);
    for (size_t i = 0; i < TABLE_ENTRIES; ++i)
        pt[i] = (base + i * RIXURI_PAGE_SIZE) | inherited;
    pd[index] = phys | RIXURI_PTE_PRESENT | RIXURI_PTE_WRITE | (inherited & RIXURI_PTE_USER);
    return pt;
}

static uint64_t *ensure_table(uint64_t *parent, size_t index, uint64_t flags) {
    uint64_t entry = parent[index];
    if (entry & RIXURI_PTE_PRESENT) {
        if (entry & PTE_PS) return NULL;
        return table_for(entry);
    }
    uint64_t phys = pmm_alloc_page();
    if (!phys || phys >= 0x40000000ULL) return NULL;
    uint64_t *table = (uint64_t *)(uintptr_t)phys;
    for (size_t i = 0; i < TABLE_ENTRIES; ++i) table[i] = 0;
    parent[index] = phys | RIXURI_PTE_PRESENT | RIXURI_PTE_WRITE | (flags & RIXURI_PTE_USER);
    return table;
}

int vmm_map_page(uint64_t va, uint64_t pa, uint64_t flags) {
    if ((va & 0xfffULL) || (pa & 0xfffULL) || !canonical(va) ||
        (pa & ~PTE_ADDR_MASK)) return -1;

    uint64_t *pml4 = early_pml4;
    uint64_t *pdpt = ensure_table(pml4, (va >> 39) & 0x1ffULL, flags);
    if (!pdpt) return -1;
    uint64_t *pd = ensure_table(pdpt, (va >> 30) & 0x1ffULL, flags);
    if (!pd) return -1;

    size_t pd_index = (size_t)((va >> 21) & 0x1ffULL);
    uint64_t *pt;
    if (pd[pd_index] & PTE_PRESENT) {
        pt = split_2m(pd, pd_index);
    } else {
        pt = ensure_table(pd, pd_index, flags);
    }
    if (!pt) return -1;

    size_t pt_index = (size_t)((va >> 12) & 0x1ffULL);
    uint64_t old = pt[pt_index];
    if (old & RIXURI_PTE_PRESENT) return -2;
    pt[pt_index] = pa | (flags & (RIXURI_PTE_PRESENT | RIXURI_PTE_WRITE | RIXURI_PTE_USER | RIXURI_PTE_NX));
    invlpg(va);
    return 0;
}

void vmm_unmap_page(uint64_t va) {
    if ((va & 0xfffULL) || !canonical(va)) return;
    uint64_t e = early_pml4[(va >> 39) & 0x1ffULL];
    if (!(e & RIXURI_PTE_PRESENT) || (e & PTE_PS)) return;
    uint64_t *pdpt = table_for(e);
    e = pdpt[(va >> 30) & 0x1ffULL];
    if (!(e & RIXURI_PTE_PRESENT) || (e & PTE_PS)) return;
    uint64_t *pd = table_for(e);
    e = pd[(va >> 21) & 0x1ffULL];
    if (!(e & RIXURI_PTE_PRESENT)) return;
    if (e & PTE_PS) return;
    uint64_t *pt = table_for(e);
    pt[(va >> 12) & 0x1ffULL] = 0;
    invlpg(va);
}

uint64_t vmm_translate(uint64_t va) {
    if (!canonical(va)) return 0;
    uint64_t e = early_pml4[(va >> 39) & 0x1ffULL];
    if (!(e & RIXURI_PTE_PRESENT)) return 0;
    uint64_t *pdpt = table_for(e);
    e = pdpt[(va >> 30) & 0x1ffULL];
    if (!(e & RIXURI_PTE_PRESENT)) return 0;
    uint64_t *pd = table_for(e);
    e = pd[(va >> 21) & 0x1ffULL];
    if (!(e & RIXURI_PTE_PRESENT)) return 0;
    if (e & PTE_PS) return (e & PTE_ADDR_MASK) + (va & 0x1fffffULL);
    uint64_t *pt = table_for(e);
    e = pt[(va >> 12) & 0x1ffULL];
    if (!(e & RIXURI_PTE_PRESENT)) return 0;
    return (e & PTE_ADDR_MASK) + (va & 0xfffULL);
}

uint64_t vmm_current_cr3(void) { return read_cr3() & PTE_ADDR_MASK; }
