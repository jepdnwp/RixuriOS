#pragma once
#include <stdint.h>

#define RIXURI_PTE_PRESENT  (1ULL << 0)
#define RIXURI_PTE_WRITE    (1ULL << 1)
#define RIXURI_PTE_USER     (1ULL << 2)
/* Software-owned leaf mapping. Cleared on borrowed identity mappings. */
#define RIXURI_PTE_OWNED   (1ULL << 9)
#define RIXURI_PTE_NX       (1ULL << 63)

void vmm_early_init(void);
uint64_t vmm_kernel_pml4(void);
int vmm_map_page_in_pml4(uint64_t pml4_phys,uint64_t virtual_address,uint64_t physical_address,uint64_t flags);
int vmm_unmap_page_in_pml4(uint64_t pml4_phys,uint64_t virtual_address);
int vmm_map_page(uint64_t virtual_address,uint64_t physical_address,uint64_t flags);
void vmm_unmap_page(uint64_t virtual_address);
uint64_t vmm_translate(uint64_t virtual_address);
uint64_t vmm_query_flags(uint64_t virtual_address);
