#include "ptmap.h"
#include "pmm.h"
#include "vmm.h"

#define PTMAP_ENTRIES 512u

void *pt_kmap(uint64_t physical_page) {
    if ((physical_page & 0xFFFULL) != 0 || physical_page >= RIXURI_MAX_PHYS_BYTES) return 0;
    return vmm_phys_ptr(physical_page);
}

void pt_kunmap(void *kernel_address) {
    (void)kernel_address;
}

uint64_t pt_read(uint64_t physical_page, unsigned index) {
    if (index >= PTMAP_ENTRIES) return 0;
    uint64_t *table = (uint64_t *)pt_kmap(physical_page);
    return table ? table[index] : 0;
}

void pt_write(uint64_t physical_page, unsigned index, uint64_t value) {
    if (index >= PTMAP_ENTRIES) return;
    uint64_t *table = (uint64_t *)pt_kmap(physical_page);
    if (table) table[index] = value;
}

