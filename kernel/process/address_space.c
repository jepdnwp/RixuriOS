#include "address_space.h"
#include "../mm/pmm.h"
#include "../mm/vmm.h"
#include <stdint.h>
static uint64_t *ptr(uint64_t p){return(uint64_t *)(uintptr_t)p;}
static int user_va(uint64_t va){return va>=0x1000ULL&&va<(1ULL<<47)&&!(va&0xfffULL);}
int address_space_create(rix_address_space_t *as){if(!as)return -1;uint64_t p=pmm_alloc_page();if(!p)return -1;uint64_t*t=ptr(p);for(unsigned i=0;i<512;i++)t[i]=0;uint64_t kp=vmm_kernel_pml4();uint64_t*kt=ptr(kp);for(unsigned i=256;i<512;i++)t[i]=kt[i];as->pml4_phys=p;return 0;}
int address_space_map(rix_address_space_t *as,uint64_t va,uint64_t pa,uint64_t flags){if(!as||!as->pml4_phys||!user_va(va)||(pa&0xfffULL))return -1;return vmm_map_page_in_pml4(as->pml4_phys,va,pa,flags|RIXURI_PTE_USER);}
void address_space_destroy(rix_address_space_t *as){if(!as||!as->pml4_phys)return;pmm_free_page(as->pml4_phys);as->pml4_phys=0;}
