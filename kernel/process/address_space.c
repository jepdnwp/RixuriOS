#include "address_space.h"
#include "../mm/pmm.h"
#include "../mm/vmm.h"
#include <stdint.h>

#define PAGE_MASK 0x000FFFFFFFFFF000ULL
#define PTE_PS (1ULL << 7)
#define TABLE_COUNT 512U
static uint64_t *ptr(uint64_t p){return(uint64_t *)(uintptr_t)p;}
static int user_va(uint64_t va){return va>=0x1000ULL&&va<(1ULL<<47)&&!(va&0xfffULL);}

static uint64_t walk_pte(const rix_address_space_t *as,uint64_t va){
 if(!as||!as->pml4_phys||va>=(1ULL<<47))return 0;uint64_t*table=ptr(as->pml4_phys);
 for(unsigned level=4;level>1;--level){uint64_t e=table[(va>>((level-1)*9+12))&0x1ffULL];if(!(e&RIXURI_PTE_PRESENT))return 0;if(level<=3&&(e&PTE_PS))return e;table=ptr(e&PAGE_MASK);}return table[(va>>12)&0x1ffULL];
}
static void free_pt(uint64_t phys){uint64_t*t=ptr(phys);for(unsigned i=0;i<TABLE_COUNT;i++)if(t[i]&RIXURI_PTE_PRESENT)pmm_free_page(t[i]&PAGE_MASK);pmm_free_page(phys);}
static void free_pd(uint64_t phys){uint64_t*t=ptr(phys);for(unsigned i=0;i<TABLE_COUNT;i++){uint64_t e=t[i];if(!(e&RIXURI_PTE_PRESENT))continue;if(e&PTE_PS)continue;free_pt(e&PAGE_MASK);}pmm_free_page(phys);}
static void free_pdpt(uint64_t phys){uint64_t*t=ptr(phys);for(unsigned i=0;i<TABLE_COUNT;i++)if(t[i]&RIXURI_PTE_PRESENT)free_pd(t[i]&PAGE_MASK);pmm_free_page(phys);}

int address_space_create(rix_address_space_t *as){
 if(!as)return -1;uint64_t p=pmm_alloc_page();if(!p)return -1;uint64_t*t=ptr(p);for(unsigned i=0;i<TABLE_COUNT;i++)t[i]=0;as->pml4_phys=p;
 uint64_t kp=vmm_kernel_pml4();uint64_t*kt=ptr(kp);
 for(unsigned i=256;i<512;i++)t[i]=kt[i];
 /* Clone the low kernel identity map instead of sharing it. User mappings can
    then split huge pages without mutating the kernel page tables. */
 uint64_t src_pdpt=kt[0]&PAGE_MASK;if(!(kt[0]&RIXURI_PTE_PRESENT))return 0;
 uint64_t new_pdpt=pmm_alloc_page();if(!new_pdpt){address_space_destroy(as);return -1;}uint64_t*dp=ptr(new_pdpt);for(unsigned i=0;i<TABLE_COUNT;i++)dp[i]=0;t[0]=new_pdpt|RIXURI_PTE_PRESENT|RIXURI_PTE_WRITE;
 uint64_t*sp=ptr(src_pdpt);
 for(unsigned i=0;i<TABLE_COUNT;i++){
  uint64_t e=sp[i];if(!(e&RIXURI_PTE_PRESENT))continue;
  uint64_t new_pd=pmm_alloc_page();if(!new_pd){address_space_destroy(as);return -1;}uint64_t*dst=ptr(new_pd);uint64_t*src=ptr(e&PAGE_MASK);for(unsigned j=0;j<TABLE_COUNT;j++)dst[j]=src[j];dp[i]=new_pd|RIXURI_PTE_PRESENT|RIXURI_PTE_WRITE;
 }
 return 0;
}

int address_space_map(rix_address_space_t *as,uint64_t va,uint64_t pa,uint64_t flags){if(!as||!as->pml4_phys||!user_va(va)||(pa&0xfffULL))return -1;return vmm_map_page_in_pml4(as->pml4_phys,va,pa,flags|RIXURI_PTE_USER);}
uint64_t address_space_translate(const rix_address_space_t *as,uint64_t va){uint64_t e=walk_pte(as,va);if(!(e&RIXURI_PTE_PRESENT))return 0;if(e&PTE_PS)return(e&PAGE_MASK)|(va&0x1fffffULL);return(e&PAGE_MASK)|(va&0xfffULL);}
uint64_t address_space_query_flags(const rix_address_space_t *as,uint64_t va){uint64_t e=walk_pte(as,va);return e&(RIXURI_PTE_PRESENT|RIXURI_PTE_WRITE|RIXURI_PTE_USER|RIXURI_PTE_NX);}

void address_space_destroy(rix_address_space_t *as){if(!as||!as->pml4_phys)return;uint64_t*t=ptr(as->pml4_phys);for(unsigned i=0;i<256;i++)if(t[i]&RIXURI_PTE_PRESENT)free_pdpt(t[i]&PAGE_MASK);pmm_free_page(as->pml4_phys);as->pml4_phys=0;}
