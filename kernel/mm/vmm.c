#include "vmm.h"
#include "pmm.h"
#include "../serial.h"
#include <stddef.h>
#define TABLE_ENTRIES 512ULL
/* UEFI may place the memory map or GOP framebuffer above 128 GiB on
 * machines with large/high-address physical memory.  Keep the early
 * identity map wide enough to access those buffers while switching CR3. */
#define IDENTITY_PML4_COUNT 4ULL
#define IDENTITY_PDPT_COUNT (IDENTITY_PML4_COUNT * TABLE_ENTRIES)
#define PAGE_MASK 0x000FFFFFFFFFF000ULL
#define PTE_PS (1ULL<<7)
#define CR0_WP (1ULL<<16)
#define CR4_PAE (1ULL<<5)
#define LEAF_FLAGS (RIXURI_PTE_PRESENT|RIXURI_PTE_WRITE|RIXURI_PTE_USER|RIXURI_PTE_NX|RIXURI_PTE_OWNED)
static uint64_t early_pml4[512] __attribute__((aligned(4096)));static uint64_t early_pdpt[IDENTITY_PML4_COUNT][512] __attribute__((aligned(4096)));static uint64_t early_pd[IDENTITY_PDPT_COUNT][512] __attribute__((aligned(4096)));static uint64_t kernel_pml4_phys;uint64_t current_pml4_phys;
static inline void write_cr3(uint64_t v){__asm__ volatile("mov %0,%%cr3"::"r"(v):"memory");}static inline void invlpg(uint64_t va){__asm__ volatile("invlpg (%0)"::"r"(va):"memory");}static inline uint64_t read_cr0(void){uint64_t v;__asm__ volatile("mov %%cr0,%0":"=r"(v));return v;}static inline void write_cr0(uint64_t v){__asm__ volatile("mov %0,%%cr0"::"r"(v):"memory");}static inline uint64_t read_cr4(void){uint64_t v;__asm__ volatile("mov %%cr4,%0":"=r"(v));return v;}static inline void write_cr4(uint64_t v){__asm__ volatile("mov %0,%%cr4"::"r"(v):"memory");}
static uint64_t *entry_table(uint64_t e){return(uint64_t *)(uintptr_t)(e&PAGE_MASK);}static int canonical48(uint64_t va){return va<(1ULL<<47)||va>=(UINT64_MAX-(1ULL<<47)+1ULL);}
static uint64_t *ensure_table(uint64_t *parent,size_t index,uint64_t flags){uint64_t e=parent[index];if(e&RIXURI_PTE_PRESENT){if(flags&RIXURI_PTE_USER)parent[index]|=RIXURI_PTE_USER;return entry_table(parent[index]);}uint64_t phys=pmm_alloc_page();if(!phys)return NULL;uint64_t*t=(uint64_t *)(uintptr_t)phys;for(size_t i=0;i<TABLE_ENTRIES;i++)t[i]=0;parent[index]=phys|RIXURI_PTE_PRESENT|RIXURI_PTE_WRITE|(flags&RIXURI_PTE_USER);return t;}
static uint64_t *split_pd_huge_page(uint64_t *pd,size_t index,uint64_t flags){uint64_t e=pd[index];if(!(e&RIXURI_PTE_PRESENT))return ensure_table(pd,index,flags);if(!(e&PTE_PS)){if(flags&RIXURI_PTE_USER)pd[index]|=RIXURI_PTE_USER;return entry_table(pd[index]);}uint64_t phys=pmm_alloc_page();if(!phys)return NULL;uint64_t*pt=(uint64_t *)(uintptr_t)phys;uint64_t base=e&PAGE_MASK;uint64_t leaf=e&(RIXURI_PTE_WRITE|RIXURI_PTE_USER|RIXURI_PTE_NX);for(size_t i=0;i<TABLE_ENTRIES;i++)pt[i]=(base+(uint64_t)i*0x1000ULL)|RIXURI_PTE_PRESENT|leaf;pd[index]=phys|RIXURI_PTE_PRESENT|RIXURI_PTE_WRITE|(e&RIXURI_PTE_USER)|(flags&RIXURI_PTE_USER);return pt;}
void vmm_early_init(void){for(size_t i=0;i<TABLE_ENTRIES;i++)early_pml4[i]=0;for(size_t n=0;n<IDENTITY_PML4_COUNT;n++){for(size_t i=0;i<TABLE_ENTRIES;i++)early_pdpt[n][i]=0;}for(size_t n=0;n<IDENTITY_PDPT_COUNT;n++)for(size_t i=0;i<TABLE_ENTRIES;i++)early_pd[n][i]=0;for(uint64_t p=0;p<IDENTITY_PML4_COUNT;p++){early_pml4[p]=(uint64_t)(uintptr_t)early_pdpt[p]|RIXURI_PTE_PRESENT|RIXURI_PTE_WRITE;for(uint64_t n=0;n<TABLE_ENTRIES;n++){uint64_t pd_index=p*TABLE_ENTRIES+n;early_pdpt[p][n]=(uint64_t)(uintptr_t)early_pd[pd_index]|RIXURI_PTE_PRESENT|RIXURI_PTE_WRITE;for(uint64_t i=0;i<TABLE_ENTRIES;i++){uint64_t pa=(pd_index*TABLE_ENTRIES+i)*0x200000ULL;early_pd[pd_index][i]=pa|RIXURI_PTE_PRESENT|RIXURI_PTE_WRITE|PTE_PS;}}}pmm_reserve_page((uint64_t)(uintptr_t)early_pml4);for(size_t p=0;p<IDENTITY_PML4_COUNT;p++)pmm_reserve_page((uint64_t)(uintptr_t)early_pdpt[p]);for(size_t n=0;n<IDENTITY_PDPT_COUNT;n++)pmm_reserve_page((uint64_t)(uintptr_t)early_pd[n]);kernel_pml4_phys=(uint64_t)(uintptr_t)early_pml4;current_pml4_phys=kernel_pml4_phys;write_cr3(current_pml4_phys);write_cr0(read_cr0()|CR0_WP);write_cr4(read_cr4()|CR4_PAE);}
uint64_t vmm_kernel_pml4(void){return kernel_pml4_phys;}
uint64_t vmm_current_pml4(void){return current_pml4_phys;}
void *vmm_phys_ptr(uint64_t physical_address){if(!physical_address||(physical_address&0xFFFULL)||physical_address>=RIXURI_MAX_PHYS_BYTES)return NULL;return(void *)(uintptr_t)physical_address;}
void vmm_switch_pml4(uint64_t pml4_phys){if(!pml4_phys)return;current_pml4_phys=pml4_phys;write_cr3(pml4_phys);}
void vmm_track_pml4(uint64_t pml4_phys){if(!pml4_phys)return;current_pml4_phys=pml4_phys;}
int vmm_map_page_in_pml4(uint64_t pml4_phys,uint64_t va,uint64_t pa,uint64_t flags){if(!pml4_phys||!canonical48(va)||(va&0xFFFULL)||(pa&0xFFFULL)||(pa&~PAGE_MASK))return -1;uint64_t*pml4=(uint64_t *)(uintptr_t)pml4_phys;uint64_t*pdpt=ensure_table(pml4,(va>>39)&0x1FFULL,flags);if(!pdpt)return -1;uint64_t*pd=ensure_table(pdpt,(va>>30)&0x1FFULL,flags);if(!pd)return -1;uint64_t*pt=split_pd_huge_page(pd,(va>>21)&0x1FFULL,flags);if(!pt)return -1;size_t idx=(size_t)((va>>12)&0x1FFULL);pt[idx]=(pa&PAGE_MASK)|(flags&LEAF_FLAGS);if(pml4_phys==current_pml4_phys)invlpg(va);return 0;}
int vmm_unmap_page_in_pml4(uint64_t pml4_phys,uint64_t va){if(!pml4_phys||!canonical48(va)||(va&0xFFFULL))return -1;uint64_t*pml4=(uint64_t *)(uintptr_t)pml4_phys;uint64_t e=pml4[(va>>39)&0x1FFULL];if(!(e&RIXURI_PTE_PRESENT))return 0;uint64_t*pdpt=entry_table(e);e=pdpt[(va>>30)&0x1FFULL];if(!(e&RIXURI_PTE_PRESENT))return 0;uint64_t*pd=entry_table(e);e=pd[(va>>21)&0x1FFULL];if(!(e&RIXURI_PTE_PRESENT)||(e&PTE_PS))return 0;uint64_t*pt=entry_table(e);pt[(va>>12)&0x1FFULL]=0;if(pml4_phys==current_pml4_phys)invlpg(va);return 0;}
int vmm_map_page(uint64_t va,uint64_t pa,uint64_t flags){return vmm_map_page_in_pml4(current_pml4_phys,va,pa,flags);}
void vmm_unmap_page(uint64_t va){(void)vmm_unmap_page_in_pml4(current_pml4_phys,va);}
uint64_t vmm_translate(uint64_t va){if(!canonical48(va))return 0;uint64_t*pml4=(uint64_t *)(uintptr_t)current_pml4_phys;uint64_t e=pml4[(va>>39)&0x1FFULL];if(!(e&RIXURI_PTE_PRESENT))return 0;uint64_t*pdpt=entry_table(e);e=pdpt[(va>>30)&0x1FFULL];if(!(e&RIXURI_PTE_PRESENT))return 0;uint64_t*pd=entry_table(e);e=pd[(va>>21)&0x1FFULL];if(!(e&RIXURI_PTE_PRESENT))return 0;if(e&PTE_PS)return(e&PAGE_MASK)+(va&0x1FFFFFULL);uint64_t*pt=entry_table(e);e=pt[(va>>12)&0x1FFULL];if(!(e&RIXURI_PTE_PRESENT))return 0;return(e&PAGE_MASK)+(va&0xFFFULL);}
uint64_t vmm_query_flags(uint64_t va){if(!canonical48(va))return 0;uint64_t*pml4=(uint64_t *)(uintptr_t)current_pml4_phys;uint64_t e=pml4[(va>>39)&0x1FFULL];if(!(e&RIXURI_PTE_PRESENT))return 0;uint64_t*pdpt=entry_table(e);e=pdpt[(va>>30)&0x1FFULL];if(!(e&RIXURI_PTE_PRESENT))return 0;uint64_t*pd=entry_table(e);e=pd[(va>>21)&0x1FFULL];if(!(e&RIXURI_PTE_PRESENT))return 0;if(e&PTE_PS)return e&(RIXURI_PTE_PRESENT|RIXURI_PTE_WRITE|RIXURI_PTE_USER|RIXURI_PTE_NX);uint64_t*pt=entry_table(e);e=pt[(va>>12)&0x1FFULL];return e&(RIXURI_PTE_PRESENT|RIXURI_PTE_WRITE|RIXURI_PTE_USER|RIXURI_PTE_NX|RIXURI_PTE_OWNED);}
/* Target-PML4 validation before any CR3 write. Returns 0 when the value is
 * safe to load into CR3; negative codes name the failing check. */
int vmm_validate_pml4(uint64_t pml4_phys){
 if(!pml4_phys)return -1;
 if(pml4_phys&0xFFFULL)return -2;
 if(pml4_phys>=RIXURI_MAX_PHYS_BYTES)return -3;
 if(pml4_phys&0xFFF0000000000000ULL)return -4;
 uint64_t*pml4=(uint64_t*)vmm_phys_ptr(pml4_phys);
 if(!pml4)return -5;
 if(!(pml4[0]&RIXURI_PTE_PRESENT))return -6;
 for(size_t i=0;i<TABLE_ENTRIES;i++){
  uint64_t e=pml4[i];
  if(!(e&RIXURI_PTE_PRESENT))continue;
  uint64_t t=e&PAGE_MASK;
  if(!t||(t&0xFFFULL)||t>=RIXURI_MAX_PHYS_BYTES)return -7;
 }
 return 0;
}
/* CR3-independent walk of an arbitrary PML4 (uses the phys window, never the
 * current CR3). 0=mapped (*out_phys/flags valid), 1=not present (missing
 * level), <0=invalid/corrupt. Handles 1 GiB and 2 MiB PS leaves. */
int vmm_walk_in_pml4(uint64_t pml4_phys,uint64_t va,uint64_t*out_pml4e,uint64_t*out_pdpte,uint64_t*out_pde,uint64_t*out_pte,uint64_t*out_phys,uint64_t*out_flags){
 if(out_pml4e){*out_pml4e=0;}if(out_pdpte){*out_pdpte=0;}if(out_pde){*out_pde=0;}if(out_pte){*out_pte=0;}if(out_phys){*out_phys=0;}if(out_flags){*out_flags=0;}
 if(!pml4_phys||(pml4_phys&0xFFFULL)||pml4_phys>=RIXURI_MAX_PHYS_BYTES)return -1;
 if(!canonical48(va))return -1;
 uint64_t*pml4=(uint64_t*)vmm_phys_ptr(pml4_phys);
 if(!pml4)return -1;
 uint64_t pml4e=pml4[(va>>39)&0x1FFULL];
 if(out_pml4e)*out_pml4e=pml4e;
 if(!(pml4e&RIXURI_PTE_PRESENT))return 1;
 uint64_t pdpt_phys=pml4e&PAGE_MASK;
 if(!pdpt_phys||(pdpt_phys&0xFFFULL)||pdpt_phys>=RIXURI_MAX_PHYS_BYTES)return -1;
 uint64_t*pdpt=(uint64_t*)vmm_phys_ptr(pdpt_phys);
 if(!pdpt)return -1;
 uint64_t pdpte=pdpt[(va>>30)&0x1FFULL];
 if(out_pdpte)*out_pdpte=pdpte;
 if(!(pdpte&RIXURI_PTE_PRESENT))return 1;
 if(pdpte&PTE_PS){
  if(out_phys)*out_phys=(pdpte&PAGE_MASK)+(va&0x3FFFFFFFULL);
  if(out_flags)*out_flags=pdpte&(RIXURI_PTE_PRESENT|RIXURI_PTE_WRITE|RIXURI_PTE_USER|RIXURI_PTE_NX);
  return 0;
 }
 uint64_t pd_phys=pdpte&PAGE_MASK;
 if(!pd_phys||(pd_phys&0xFFFULL)||pd_phys>=RIXURI_MAX_PHYS_BYTES)return -1;
 uint64_t*pd=(uint64_t*)vmm_phys_ptr(pd_phys);
 if(!pd)return -1;
 uint64_t pde=pd[(va>>21)&0x1FFULL];
 if(out_pde)*out_pde=pde;
 if(!(pde&RIXURI_PTE_PRESENT))return 1;
 if(pde&PTE_PS){
  if(out_phys)*out_phys=(pde&PAGE_MASK)+(va&0x1FFFFFULL);
  if(out_flags)*out_flags=pde&(RIXURI_PTE_PRESENT|RIXURI_PTE_WRITE|RIXURI_PTE_USER|RIXURI_PTE_NX);
  return 0;
 }
 uint64_t pt_phys=pde&PAGE_MASK;
 if(!pt_phys||(pt_phys&0xFFFULL)||pt_phys>=RIXURI_MAX_PHYS_BYTES)return -1;
 uint64_t*pt=(uint64_t*)vmm_phys_ptr(pt_phys);
 if(!pt)return -1;
 uint64_t pte=pt[(va>>12)&0x1FFULL];
 if(out_pte)*out_pte=pte;
 if(!(pte&RIXURI_PTE_PRESENT))return 1;
 if(out_phys)*out_phys=(pte&PAGE_MASK)+(va&0xFFFULL);
 if(out_flags)*out_flags=pte&(RIXURI_PTE_PRESENT|RIXURI_PTE_WRITE|RIXURI_PTE_USER|RIXURI_PTE_NX|RIXURI_PTE_OWNED);
 return 0;
}
/* Serial-only single-VA walk dump: VA -> PML4E/PDPTE/PDE/PTE -> phys+flags.
 * Deliberately NOT on screen: on physical UC-VRAM consoles every line costs
 * a full-screen repaint; ~40 detail lines would look like a freeze. The
 * screen keeps the short verdict/marker lines (via kernel_log in the
 * caller); full chains stay on COM1 for capture. */
void vmm_log_walk(uint64_t pml4_phys,uint64_t va,const char*label){
 uint64_t pml4e=0,pdpte=0,pde=0,pte=0,phys=0,flags=0;
 int rc=vmm_walk_in_pml4(pml4_phys,va,&pml4e,&pdpte,&pde,&pte,&phys,&flags);
 serial_write("WALK ");
 serial_write(label?label:"?");
 serial_write(" va=");
 serial_write_hex(va);
 serial_write(" rc=");
 if(rc<0)serial_write("-1");else serial_write_dec((uint64_t)rc);
 serial_write(" pml4e=");
 serial_write_hex(pml4e);
 serial_write(" pdpte=");
 serial_write_hex(pdpte);
 serial_write(" pde=");
 serial_write_hex(pde);
 serial_write(" pte=");
 serial_write_hex(pte);
 serial_write(" phys=");
 serial_write_hex(phys);
 serial_write(" flags=");
 serial_write_hex(flags);
 {char pwun[5];pwun[0]=(flags&RIXURI_PTE_PRESENT)?'P':'.';pwun[1]=(flags&RIXURI_PTE_WRITE)?'W':'.';pwun[2]=(flags&RIXURI_PTE_USER)?'U':'.';pwun[3]=(flags&RIXURI_PTE_NX)?'X':'.';pwun[4]=0;serial_write(" pwun=");serial_write(pwun);}
 serial_write(rc==0?" MAPPED\r\n":" UNMAPPED\r\n");
}
/* Serial-only kernel-vs-target PML4 comparison: every non-zero slot (see
 * vmm_log_walk comment for why detail stays off screen). */
void vmm_log_pml4_compare(uint64_t a_phys,uint64_t b_phys){ uint64_t*a=(uint64_t*)vmm_phys_ptr(a_phys);
 uint64_t*b=(uint64_t*)vmm_phys_ptr(b_phys);
 if(!a||!b){serial_write("PML4CMP: unreadable\r\n");return;}
 for(size_t i=0;i<TABLE_ENTRIES;i++){
  uint64_t ea=a[i],eb=b[i];
  if(!ea&&!eb)continue;
  serial_write("PML4CMP idx=");
  serial_write_dec((uint64_t)i);
  serial_write(" kern=");
  serial_write_hex(ea);
  serial_write(" targ=");
  serial_write_hex(eb);
  serial_write(ea==eb?" SAME\r\n":" DIFF\r\n");
 }
}
/* Unconditional dump of PML4 slots [first,last): kernel vs target side by
 * side, zero slots included, so missing kernel mappings are explicit. */
void vmm_log_pml4_range(uint64_t a_phys,uint64_t b_phys,unsigned first,unsigned last){
 uint64_t*a=(uint64_t*)vmm_phys_ptr(a_phys);
 uint64_t*b=(uint64_t*)vmm_phys_ptr(b_phys);
 if(!a||!b){serial_write("PML4DUMP: unreadable\r\n");return;}
 if(first>TABLE_ENTRIES)first=(unsigned)TABLE_ENTRIES;
 if(last>TABLE_ENTRIES||last<first)last=(unsigned)TABLE_ENTRIES;
 for(unsigned i=first;i<last;i++){
  uint64_t ea=a[i],eb=b[i];
  serial_write("PML4DUMP idx=");
  serial_write_dec((uint64_t)i);
  serial_write(" kern=");
  serial_write_hex(ea);
  serial_write(" targ=");
  serial_write_hex(eb);
  serial_write(ea==eb?" SAME\r\n":" DIFF\r\n");
 }
}
