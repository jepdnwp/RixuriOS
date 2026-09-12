#include "address_space.h"
#include "../mm/pmm.h"
#include "../mm/vmm.h"
#include "../mm/ptmap.h"
#include "../arch/x86_64/smp.h"
#include <stdint.h>

#define PAGE_MASK 0x000FFFFFFFFFF000ULL
#define PTE_PS (1ULL << 7)
#define TABLE_COUNT 512U
/* Kernel-shared PML4 ranges (borrowed, never freed): low identity PML4[0]
 * (0..512 GiB, covers all RAM <=128 GiB + low MMIO), PML4[2..3]
 * (1..2 TiB) and high half 256..511 (kernel high mappings such as LAPIC).
 * PML4[1] (512 GiB..1 TiB) stays PRIVATE: the user image links at
 * 0x8000000000 == PML4[1] (2^39), so sharing PML4[1] would mix user leaves
 * into the kernel template. User range is PML4[1] plus 4..255
 * (PML4[255]=stack). Sharing (not cloning) keeps later kernel mappings
 * (e.g. IOAPIC after PID1 creation) visible in every process. */
#define KERNEL_HIGH_PML4_BASE 256U
#define USER_PML4_BASE 4U
#define USER_PML4_END KERNEL_HIGH_PML4_BASE
#define USER_IMAGE_PML4 1U
#define USER_STACK_PML4 255U
static uint64_t *ptr(uint64_t p){return(uint64_t *)pt_kmap(p);}
static int user_va(uint64_t va){return va>=0x1000ULL&&va<(1ULL<<47)&&!(va&0xfffULL);}
static int kernel_shared_pml4(unsigned idx){return idx==0||idx==2||idx==3||idx>=KERNEL_HIGH_PML4_BASE;}
static int user_pml4_ok(uint64_t va){unsigned idx=(unsigned)((va>>39)&0x1ffULL);return idx==USER_IMAGE_PML4||(idx>=USER_PML4_BASE&&idx<USER_PML4_END);}

static uint64_t walk_pte(const rix_address_space_t *as,uint64_t va){
 if(!as||!as->pml4_phys||va>=(1ULL<<47))return 0;
 uint64_t*table=ptr(as->pml4_phys);
 for(unsigned level=4;level>1;--level){uint64_t e=table[(va>>((level-1)*9+12))&0x1ffULL];if(!(e&RIXURI_PTE_PRESENT))return 0;if(level<=3&&(e&PTE_PS))return e;table=ptr(e&PAGE_MASK);}return table[(va>>12)&0x1ffULL];
}

/* User-range teardown: every table under the private PML4 slots
 * (PML4[1] image + PML4[4..255]) is process-owned, so free unconditionally
 * (intermediates carry no OWNED bit from the mapper). Leaf data pages are
 * freed only when OWNED (shared leaves survive). */
static void free_user_pt(uint64_t phys){uint64_t*t=ptr(phys);if(!t){pmm_free_page(phys);return;}for(unsigned i=0;i<TABLE_COUNT;i++){uint64_t e=t[i];if((e&RIXURI_PTE_PRESENT)&&(e&RIXURI_PTE_OWNED))pmm_free_page(e&PAGE_MASK);}pmm_free_page(phys);}
static void free_user_pd(uint64_t phys){uint64_t*t=ptr(phys);if(!t){pmm_free_page(phys);return;}for(unsigned i=0;i<TABLE_COUNT;i++){uint64_t e=t[i];if(!(e&RIXURI_PTE_PRESENT))continue;if(e&PTE_PS)continue;free_user_pt(e&PAGE_MASK);}pmm_free_page(phys);}
static void free_user_pdpt(uint64_t phys){uint64_t*t=ptr(phys);if(!t){pmm_free_page(phys);return;}for(unsigned i=0;i<TABLE_COUNT;i++){uint64_t e=t[i];if(!(e&RIXURI_PTE_PRESENT))continue;if(e&PTE_PS)continue;free_user_pd(e&PAGE_MASK);}pmm_free_page(phys);}
/* Teardown mirror of the sync rule: USER-marked slots are process-owned and
 * freed; slots identical to the live kernel value are borrowed references
 * and must never be freed (freeing them would hand kernel-owned table pages
 * back to PMM while the kernel still walks them); anything else present is
 * process-owned (duplicates, legacy clones) and freed. */
static void destroy_user_tables(rix_address_space_t *as){if(!as||!as->pml4_phys)return;uint64_t*t=ptr(as->pml4_phys);if(t){uint64_t*kt=ptr(vmm_kernel_pml4());for(unsigned i=0;i<TABLE_COUNT;i++){uint64_t e=t[i];if(!(e&RIXURI_PTE_PRESENT))continue;if(e&RIXURI_PTE_USER){free_user_pdpt(e&PAGE_MASK);continue;}if(kt){uint64_t ke=kt[i],want=0;if(ke&RIXURI_PTE_PRESENT)want=ke&~(RIXURI_PTE_USER|RIXURI_PTE_OWNED);if(e==want)continue;}free_user_pdpt(e&PAGE_MASK);}}pmm_free_page(as->pml4_phys);as->pml4_phys=0;}

int address_space_create(rix_address_space_t *as){
 if(!as)return -1;
 as->pml4_phys=0;uint64_t pml4=pmm_alloc_page();if(!pml4)return -2;uint64_t*t=ptr(pml4);if(!t){pmm_free_page(pml4);return -2;}for(unsigned i=0;i<TABLE_COUNT;i++)t[i]=0;as->pml4_phys=pml4;
 /* Borrow kernel mappings as shared references through the phys window.
  * No CR3 switch: pt_kmap is CR3-independent. Kernel entries are forced
  * supervisor (USER cleared) and borrowed (OWNED cleared); W^X/NX leaf
  * policy is preserved verbatim. Distinct codes: -2 PML4 alloc,
  * -3 no/unreadable kernel template, -4 invalid kernel entry. */
 uint64_t kp=vmm_kernel_pml4();if(!kp){pmm_free_page(pml4);as->pml4_phys=0;return -3;}
 uint64_t*kt=ptr(kp);if(!kt){pmm_free_page(pml4);as->pml4_phys=0;return -3;}
 for(unsigned i=0;i<TABLE_COUNT;i++){if(!kernel_shared_pml4(i))continue;uint64_t e=kt[i];if(!(e&RIXURI_PTE_PRESENT))continue;uint64_t tab=e&PAGE_MASK;if(!tab||(tab&0xfffULL))goto fail;t[i]=e&~(RIXURI_PTE_USER|RIXURI_PTE_OWNED);}
 return 0;
fail:
 as->pml4_phys=0;{uint64_t*tt=ptr(pml4);if(tt)for(unsigned i=0;i<TABLE_COUNT;i++)tt[i]=0;}pmm_free_page(pml4);return -4;
}

static int map_page(rix_address_space_t *as,uint64_t va,uint64_t pa,uint64_t flags,int owned){if(!as||!as->pml4_phys||!user_va(va)||!user_pml4_ok(va)||(pa&0xfffULL))return -1;if(address_space_query_flags(as,va)&RIXURI_PTE_PRESENT)return -1;flags|=RIXURI_PTE_PRESENT|RIXURI_PTE_USER;if(owned)flags|=RIXURI_PTE_OWNED;else flags&=~RIXURI_PTE_OWNED;return vmm_map_page_in_pml4(as->pml4_phys,va,pa,flags);}
int address_space_map(rix_address_space_t *as,uint64_t va,uint64_t pa,uint64_t flags){return map_page(as,va,pa,flags,1);}
int address_space_map_shared(rix_address_space_t *as,uint64_t va,uint64_t pa,uint64_t flags){return map_page(as,va,pa,flags,0);}
int address_space_update_flags(rix_address_space_t *as,uint64_t va,uint64_t flags){if(!as||!as->pml4_phys||!user_va(va)||!user_pml4_ok(va))return -1;uint64_t old=address_space_query_flags(as,va);if((old&(RIXURI_PTE_PRESENT|RIXURI_PTE_USER|RIXURI_PTE_OWNED))!=(RIXURI_PTE_PRESENT|RIXURI_PTE_USER|RIXURI_PTE_OWNED))return -1;uint64_t pa=address_space_translate(as,va)&PAGE_MASK;if(!pa)return -1;flags|=RIXURI_PTE_PRESENT|RIXURI_PTE_USER|RIXURI_PTE_OWNED;return vmm_map_page_in_pml4(as->pml4_phys,va,pa,flags);}
int address_space_unmap(rix_address_space_t *as,uint64_t va){if(!as||!as->pml4_phys||!user_va(va)||!user_pml4_ok(va))return -1;uint64_t old=address_space_query_flags(as,va);if(!(old&RIXURI_PTE_PRESENT))return -1;uint64_t pa=address_space_translate(as,va)&PAGE_MASK;uint64_t*pml4=ptr(as->pml4_phys);uint64_t e=pml4[(va>>39)&0x1ffULL];if(!(e&RIXURI_PTE_PRESENT))return -1;uint64_t*pdpt=ptr(e&PAGE_MASK);e=pdpt[(va>>30)&0x1ffULL];if(!(e&RIXURI_PTE_PRESENT))return -1;uint64_t*pd=ptr(e&PAGE_MASK);e=pd[(va>>21)&0x1ffULL];if(!(e&RIXURI_PTE_PRESENT)||e&PTE_PS)return -1;uint64_t*pt=ptr(e&PAGE_MASK);pt[(va>>12)&0x1ffULL]=0;if(old&RIXURI_PTE_OWNED)pmm_free_page(pa);
/* Phase D2 TLB discipline (single choke point for all user unmaps: brk
 * shrink/rollback, shm unmap/destroy paths). The local entry is flushed
 * when this root is current (without it, same-task reuse of the VA right
 * after unmap could hit a stale TLB entry even on UP). When more than one
 * CPU is online, broadcast as well: another CPU may hold the entry (its
 * own invlpg runs inside smp_shootdown, so no separate local flush is
 * needed on that path). Callers run in syscall/process context, never in
 * IRQ context; APs ack without locks, so no deadlock. Revisit when
 * threads migrate across CPUs (P5): the hook stays, the gate condition
 * may need the migration-aware form. */
if(smp_online_count()>1)(void)smp_shootdown(va);
else if(as->pml4_phys==vmm_current_pml4())vmm_invlpg(va);
return 0;}
uint64_t address_space_translate(const rix_address_space_t *as,uint64_t va){uint64_t e=walk_pte(as,va);if(!(e&RIXURI_PTE_PRESENT))return 0;if(e&PTE_PS)return(e&PAGE_MASK)|(va&0x1fffffULL);return(e&PAGE_MASK)|(va&0xfffULL);}
uint64_t address_space_query_flags(const rix_address_space_t *as,uint64_t va){uint64_t e=walk_pte(as,va);return e&(RIXURI_PTE_PRESENT|RIXURI_PTE_WRITE|RIXURI_PTE_USER|RIXURI_PTE_NX|RIXURI_PTE_OWNED);}
void address_space_destroy(rix_address_space_t *as){destroy_user_tables(as);}
int address_space_sync_kernel(rix_address_space_t *as){
 if(!as||!as->pml4_phys||(as->pml4_phys&0xfffULL))return -1;
 uint64_t kp=vmm_kernel_pml4();if(!kp||kp==as->pml4_phys)return -1;
 uint64_t*t=ptr(as->pml4_phys);uint64_t*kt=ptr(kp);
 if(!t||!kt)return -1;
 int changed=0;
 for(unsigned i=0;i<TABLE_COUNT;i++){
  if(i==USER_IMAGE_PML4||i==USER_STACK_PML4)continue;
  uint64_t have=t[i];
  /* User-owned slots (USER set by the mapper on every user table) always
   * win; the kernel half never carries USER, so this guard is exact. */
  if(have&RIXURI_PTE_USER)continue;
  uint64_t ke=kt[i],want=0;
  if(ke&RIXURI_PTE_PRESENT){
   uint64_t tab=ke&PAGE_MASK;
   if(!tab||(tab&0xfffULL))continue;
   want=ke&~(RIXURI_PTE_USER|RIXURI_PTE_OWNED);
  }
  if(have!=want){t[i]=want;changed++;}
 }
 return changed;
}

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
        uint8_t *src = (uint8_t *)pt_kmap(entry & PAGE_MASK);
        uint8_t *dst = (uint8_t *)pt_kmap(page);
        if (!src || !dst) { pmm_free_page(page); return -1; }
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
