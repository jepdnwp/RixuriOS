#include "address_space.h"
#include "kernel.h"
#include "../mm/pmm.h"
#include "../mm/vmm.h"
#include <stdint.h>

#define PAGE_MASK 0x000FFFFFFFFFF000ULL
#define PTE_PS (1ULL << 7)
#define TABLE_COUNT 512U
static uint64_t *ptr(uint64_t p){return(uint64_t *)vmm_phys_ptr(p);}
static int user_va(uint64_t va){return va>=0x1000ULL&&va<(1ULL<<47)&&!(va&0xfffULL);}

static uint64_t walk_pte(const rix_address_space_t *as,uint64_t va){
 if(!as||!as->pml4_phys||va>=(1ULL<<47))return 0;
 uint64_t*table=ptr(as->pml4_phys);
 for(unsigned level=4;level>1;--level){uint64_t e=table[(va>>((level-1)*9+12))&0x1ffULL];if(!(e&RIXURI_PTE_PRESENT))return 0;if(level<=3&&(e&PTE_PS))return e;table=ptr(e&PAGE_MASK);}return table[(va>>12)&0x1ffULL];
}
static void free_pt(uint64_t phys){uint64_t*t=ptr(phys);for(unsigned i=0;i<TABLE_COUNT;i++){uint64_t e=t[i];if((e&RIXURI_PTE_PRESENT)&&(e&RIXURI_PTE_OWNED))pmm_free_page(e&PAGE_MASK);}pmm_free_page(phys);}
static void free_pd(uint64_t phys){uint64_t*t=ptr(phys);for(unsigned i=0;i<TABLE_COUNT;i++){uint64_t e=t[i];if(!(e&RIXURI_PTE_PRESENT))continue;if(e&PTE_PS)continue;free_pt(e&PAGE_MASK);}pmm_free_page(phys);}
static void free_pdpt(uint64_t phys){uint64_t*t=ptr(phys);for(unsigned i=0;i<TABLE_COUNT;i++)if(t[i]&RIXURI_PTE_PRESENT)free_pd(t[i]&PAGE_MASK);pmm_free_page(phys);}
static void destroy_user_tables(rix_address_space_t *as){if(!as||!as->pml4_phys)return;serial_write("AS destroy pml4=");serial_write_hex(as->pml4_phys);serial_write("\r\n");uint64_t*t=ptr(as->pml4_phys);for(unsigned i=0;i<256;i++)if(t[i]&RIXURI_PTE_PRESENT)free_pdpt(t[i]&PAGE_MASK);pmm_free_page(as->pml4_phys);as->pml4_phys=0;}

int address_space_create(rix_address_space_t *as){
 if(!as)return -1;
 as->pml4_phys=0;uint64_t pml4=pmm_alloc_page();if(!pml4)return -1;serial_write("AS create pml4=");serial_write_hex(pml4);serial_write("\r\n");uint64_t*t=ptr(pml4);for(unsigned i=0;i<TABLE_COUNT;i++)t[i]=0;as->pml4_phys=pml4;
 uint64_t kp=vmm_kernel_pml4(),active=vmm_current_pml4();if(active!=kp){serial_write("AS template active=");serial_write_hex(active);serial_write(" kernel=");serial_write_hex(kp);serial_write(" translate=");serial_write_hex(vmm_translate(kp+0x800ULL));serial_write("\r\n");}int switched=active!=kp;if(switched)vmm_switch_pml4(kp);
 uint64_t*kt=ptr(kp);for(unsigned i=256;i<512;i++)t[i]=kt[i];
 if(!(kt[0]&RIXURI_PTE_PRESENT))goto fail;
 uint64_t src_pdpt=kt[0]&PAGE_MASK;uint64_t new_pdpt=pmm_alloc_page();if(!new_pdpt)goto fail;
 uint64_t*dp=ptr(new_pdpt);for(unsigned i=0;i<TABLE_COUNT;i++)dp[i]=0;t[0]=new_pdpt|RIXURI_PTE_PRESENT|RIXURI_PTE_WRITE;
 uint64_t*sp=ptr(src_pdpt);unsigned built=0;
 for(unsigned i=0;i<TABLE_COUNT;i++){
  uint64_t e=sp[i];
  if(!(e&RIXURI_PTE_PRESENT)){built=i+1;continue;}
  if(e&PTE_PS){dp[i]=e;built=i+1;continue;}
  uint64_t new_pd=pmm_alloc_page();
  if(!new_pd){for(unsigned j=0;j<built;j++)if(dp[j]&RIXURI_PTE_PRESENT&&!(dp[j]&PTE_PS))free_pd(dp[j]&PAGE_MASK);pmm_free_page(new_pdpt);t[0]=0;pmm_free_page(as->pml4_phys);as->pml4_phys=0;goto restore;}
  uint64_t*dst=ptr(new_pd);uint64_t*src=ptr(e&PAGE_MASK);for(unsigned j=0;j<TABLE_COUNT;j++)dst[j]=src[j]&~RIXURI_PTE_OWNED;dp[i]=new_pd|RIXURI_PTE_PRESENT|RIXURI_PTE_WRITE;built=i+1;
 }
 restore:if(switched)vmm_switch_pml4(active);return as->pml4_phys?0:-1;
 fail:destroy_user_tables(as);goto restore;
}

static int map_page(rix_address_space_t *as,uint64_t va,uint64_t pa,uint64_t flags,int owned){if(!as||!as->pml4_phys||!user_va(va)||(pa&0xfffULL))return -1;if(address_space_query_flags(as,va)&RIXURI_PTE_PRESENT)return -1;flags|=RIXURI_PTE_PRESENT|RIXURI_PTE_USER;if(owned)flags|=RIXURI_PTE_OWNED;else flags&=~RIXURI_PTE_OWNED;return vmm_map_page_in_pml4(as->pml4_phys,va,pa,flags);}
int address_space_map(rix_address_space_t *as,uint64_t va,uint64_t pa,uint64_t flags){return map_page(as,va,pa,flags,1);}
int address_space_map_shared(rix_address_space_t *as,uint64_t va,uint64_t pa,uint64_t flags){return map_page(as,va,pa,flags,0);}
int address_space_update_flags(rix_address_space_t *as,uint64_t va,uint64_t flags){if(!as||!as->pml4_phys||!user_va(va))return -1;uint64_t old=address_space_query_flags(as,va);if((old&(RIXURI_PTE_PRESENT|RIXURI_PTE_USER|RIXURI_PTE_OWNED))!=(RIXURI_PTE_PRESENT|RIXURI_PTE_USER|RIXURI_PTE_OWNED))return -1;uint64_t pa=address_space_translate(as,va)&PAGE_MASK;if(!pa)return -1;flags|=RIXURI_PTE_PRESENT|RIXURI_PTE_USER|RIXURI_PTE_OWNED;return vmm_map_page_in_pml4(as->pml4_phys,va,pa,flags);}
int address_space_unmap(rix_address_space_t *as,uint64_t va){if(!as||!as->pml4_phys||!user_va(va))return -1;uint64_t old=address_space_query_flags(as,va);if(!(old&RIXURI_PTE_PRESENT))return -1;uint64_t pa=address_space_translate(as,va)&PAGE_MASK;uint64_t*pml4=ptr(as->pml4_phys);uint64_t e=pml4[(va>>39)&0x1ffULL];if(!(e&RIXURI_PTE_PRESENT))return -1;uint64_t*pdpt=ptr(e&PAGE_MASK);e=pdpt[(va>>30)&0x1ffULL];if(!(e&RIXURI_PTE_PRESENT))return -1;uint64_t*pd=ptr(e&PAGE_MASK);e=pd[(va>>21)&0x1ffULL];if(!(e&RIXURI_PTE_PRESENT)||e&PTE_PS)return -1;uint64_t*pt=ptr(e&PAGE_MASK);pt[(va>>12)&0x1ffULL]=0;if(old&RIXURI_PTE_OWNED)pmm_free_page(pa);return 0;}
uint64_t address_space_translate(const rix_address_space_t *as,uint64_t va){uint64_t e=walk_pte(as,va);if(!(e&RIXURI_PTE_PRESENT))return 0;if(e&PTE_PS)return(e&PAGE_MASK)|(va&0x1fffffULL);return(e&PAGE_MASK)|(va&0xfffULL);}
uint64_t address_space_query_flags(const rix_address_space_t *as,uint64_t va){uint64_t e=walk_pte(as,va);return e&(RIXURI_PTE_PRESENT|RIXURI_PTE_WRITE|RIXURI_PTE_USER|RIXURI_PTE_NX|RIXURI_PTE_OWNED);}
void address_space_destroy(rix_address_space_t *as){destroy_user_tables(as);}

static int clone_user_level(const uint64_t *source, unsigned level, uint64_t base,
                            rix_address_space_t *destination) {
    uint64_t step = 1ULL << ((level - 1u) * 9u + 12u);
    for (unsigned i = 0; i < TABLE_COUNT; ++i) {
        uint64_t entry = source[i];
        if (!(entry & RIXURI_PTE_PRESENT) || !(entry & RIXURI_PTE_USER)) continue;
        uint64_t va = base + (uint64_t)i * step;
        if (level > 1u) {
            if (entry & PTE_PS) return -1;
            if (clone_user_level(ptr(entry & PAGE_MASK), level - 1u, va, destination) != 0) return -1;
            continue;
        }
        if (entry & PTE_PS) return -1;
        uint64_t page = pmm_alloc_page();
        if (!page) return -1;
        uint8_t *src = (uint8_t *)(uintptr_t)(entry & PAGE_MASK);
        uint8_t *dst = (uint8_t *)(uintptr_t)page;
        for (size_t j = 0; j < 4096u; ++j) dst[j] = src[j];
        uint64_t flags = entry & (RIXURI_PTE_PRESENT | RIXURI_PTE_WRITE |
                                  RIXURI_PTE_USER | RIXURI_PTE_NX);
        if (address_space_map(destination, va, page, flags) != 0) {
            pmm_free_page(page);
            return -1;
        }
    }
    return 0;
}

int address_space_clone(const rix_address_space_t *source, rix_address_space_t *destination) {
    if (!source || !source->pml4_phys || !destination) return -1;
    if (address_space_create(destination) != 0) return -1;
    if (clone_user_level(ptr(source->pml4_phys), 4u, 0, destination) != 0) {
        address_space_destroy(destination);
        return -1;
    }
    return 0;
}
