#include "vmm.h"
#include "pmm.h"
#include <stddef.h>

#define TABLE_ENTRIES 512
#define PTE_ADDR_MASK 0x000FFFFFFFFFF000ULL
#define PTE_PS (1ULL << 7)

static uint64_t early_pml4[512] __attribute__((aligned(4096)));
static uint64_t early_pdpt[512] __attribute__((aligned(4096)));
static uint64_t early_pd[512] __attribute__((aligned(4096)));

static inline uint64_t read_cr3(void) { uint64_t v; __asm__ volatile("mov %%cr3,%0":"=r"(v)); return v; }
static inline void write_cr3(uint64_t v) { __asm__ volatile("mov %0,%%cr3"::"r"(v):"memory"); }
static inline void invlpg(uint64_t va) { __asm__ volatile("invlpg (%0)"::"r"(va):"memory"); }

static uint64_t *table_for(uint64_t entry) { return (uint64_t *)(uintptr_t)(entry & PTE_ADDR_MASK); }

void vmm_early_init(void) {
    for (size_t i=0;i<512;i++) { early_pml4[i]=0; early_pdpt[i]=0; early_pd[i]=0; }
    early_pml4[0] = (uint64_t)(uintptr_t)early_pdpt | RIXURI_PTE_PRESENT | RIXURI_PTE_WRITE;
    early_pdpt[0] = (uint64_t)(uintptr_t)early_pd | RIXURI_PTE_PRESENT | RIXURI_PTE_WRITE;
    /* Identity-map the first 1 GiB with 2 MiB pages. This is the temporary map
       used until the full VMM takes ownership of address spaces. */
    for (uint64_t i=0;i<512;i++)
        early_pd[i] = (i * 0x200000ULL) | RIXURI_PTE_PRESENT | RIXURI_PTE_WRITE | PTE_PS;
    write_cr3((uint64_t)(uintptr_t)early_pml4);
}

uint64_t vmm_kernel_pml4(void) { return (uint64_t)(uintptr_t)early_pml4; }

static uint64_t *ensure_table(uint64_t *parent, size_t index, uint64_t flags) {
    uint64_t e=parent[index];
    if (e & RIXURI_PTE_PRESENT) return table_for(e);
    uint64_t phys=pmm_alloc_page();
    if (!phys) return NULL;
    uint64_t *table=(uint64_t *)(uintptr_t)phys;
    for(size_t i=0;i<512;i++) table[i]=0;
    parent[index]=phys | RIXURI_PTE_PRESENT | RIXURI_PTE_WRITE | (flags & RIXURI_PTE_USER);
    return table;
}

int vmm_map_page(uint64_t va,uint64_t pa,uint64_t flags) {
    if ((va & 0xfff) || (pa & 0xfff) || va >= (1ULL<<48)) return -1;
    uint64_t *pml4=early_pml4;
    uint64_t *pdpt=ensure_table(pml4,(va>>39)&0x1ff,flags); if(!pdpt)return -1;
    uint64_t *pd=ensure_table(pdpt,(va>>30)&0x1ff,flags); if(!pd)return -1;
    uint64_t *pt=ensure_table(pd,(va>>21)&0x1ff,flags); if(!pt)return -1;
    size_t idx=(va>>12)&0x1ff;
    pt[idx]=(pa & PTE_ADDR_MASK) | (flags & (RIXURI_PTE_PRESENT|RIXURI_PTE_WRITE|RIXURI_PTE_USER|RIXURI_PTE_NX));
    invlpg(va);
    return 0;
}

void vmm_unmap_page(uint64_t va) {
    uint64_t *pdpt=table_for(early_pml4[(va>>39)&0x1ff]);
    if(!pdpt || !(early_pml4[(va>>39)&0x1ff]&RIXURI_PTE_PRESENT))return;
    uint64_t *pd=table_for(pdpt[(va>>30)&0x1ff]); if(!pd)return;
    uint64_t *pt=table_for(pd[(va>>21)&0x1ff]); if(!pt)return;
    pt[(va>>12)&0x1ff]=0; invlpg(va);
}

uint64_t vmm_translate(uint64_t va) {
    uint64_t e=early_pml4[(va>>39)&0x1ff]; if(!(e&RIXURI_PTE_PRESENT))return 0;
    uint64_t *pdpt=table_for(e); e=pdpt[(va>>30)&0x1ff]; if(!(e&RIXURI_PTE_PRESENT))return 0;
    uint64_t *pd=table_for(e); e=pd[(va>>21)&0x1ff]; if(!(e&RIXURI_PTE_PRESENT))return 0;
    if(e&PTE_PS) return (e&PTE_ADDR_MASK)+(va&0x1fffffULL);
    uint64_t *pt=table_for(e); e=pt[(va>>12)&0x1ff]; if(!(e&RIXURI_PTE_PRESENT))return 0;
    return (e&PTE_ADDR_MASK)+(va&0xfffULL);
}
