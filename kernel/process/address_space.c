#include "address_space.h"
#include "../mm/pmm.h"
#include "../mm/vmm.h"
#include <stdint.h>

static uint64_t table_page(void){ return pmm_alloc_page(); }
static uint64_t *phys_ptr(uint64_t p){ return (uint64_t *)(uintptr_t)p; }
int address_space_create(rix_address_space_t *as){ if(!as)return -1;uint64_t p=table_page();if(!p)return -1;uint64_t *t=phys_ptr(p);for(unsigned i=0;i<512;i++)t[i]=0;as->pml4_phys=p;return 0; }
int address_space_map(rix_address_space_t *as,uint64_t va,uint64_t pa,uint64_t flags){ if(!as||!as->pml4_phys)return -1; /* temporary: only validate canonical/user range; full hierarchical mapping follows VMM integration */ if((va>>47)!=0)return -1;return vmm_map_page(va,pa,flags|RIXURI_PTE_USER); }
void address_space_destroy(rix_address_space_t *as){ if(!as)return;if(as->pml4_phys)pmm_free_page(as->pml4_phys);as->pml4_phys=0; }
