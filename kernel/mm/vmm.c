#include "vmm.h"
#include "pmm.h"
#include "../sync/lock.h"
#include "../sync/lockdep.h"
#include "../serial.h"
#include "kernel.h"
#include <stddef.h>
#define TABLE_ENTRIES 512ULL
/* UEFI may place the memory map or GOP framebuffer above 128 GiB on
 * machines with large/high-address physical memory.  Keep the early
 * identity map wide enough to access those buffers while switching CR3. */
#define IDENTITY_PML4_COUNT 2ULL
#define IDENTITY_PDPT_COUNT (IDENTITY_PML4_COUNT * TABLE_ENTRIES)
#define PAGE_MASK 0x000FFFFFFFFFF000ULL
#define PTE_PS (1ULL<<7)
#define CR0_WP (1ULL<<16)
#define CR0_EM (1ULL<<2)
#define CR0_TS (1ULL<<3)
#define CR4_PAE (1ULL<<5)
#define CR4_PGE (1ULL<<7)
#define CR4_LA57 (1ULL<<12)
#define CR4_OSFXSR (1ULL<<9)
#define CR4_OSXMMEXCPT (1ULL<<10)
#define EFER_MSR 0xC0000080u
#define EFER_NXE (1ULL<<11)
#define LEAF_FLAGS (RIXURI_PTE_PRESENT|RIXURI_PTE_WRITE|RIXURI_PTE_USER|RIXURI_PTE_PWT|RIXURI_PTE_PCD|RIXURI_PTE_NX|RIXURI_PTE_OWNED)
/* Table-mutation lock (irqsave): serializes ensure/split/install so two
 * CPUs cannot double-allocate a table or interleave a split. Ordering
 * is vmm -> pmm (table allocs); never reversed. Static zero-init is
 * the unlocked state, so protect_kernel_sections() inside early init
 * may already use it. current_pml4_phys discipline (per-CPU writer +
 * scheduler lock in practice) is systematized with the scheduler
 * work; the MMIO registry above this layer still needs its own lock
 * (kept separate to avoid nesting with this one). */
static rix_spinlock_t vmm_map_lock;
/* lockdep rank 10: nests inside PMM (30), never inside heap. */
static unsigned vmm_map_lockdep_class;
static uint64_t early_pml4[512] __attribute__((aligned(4096)));static uint64_t early_pdpt[IDENTITY_PML4_COUNT][512] __attribute__((aligned(4096)));static uint64_t early_pd[IDENTITY_PDPT_COUNT][512] __attribute__((aligned(4096)));static uint64_t kernel_pml4_phys;uint64_t current_pml4_phys;
static inline void write_cr3(uint64_t v){__asm__ volatile("mov %0,%%cr3"::"r"(v):"memory");}static inline void invlpg(uint64_t va){__asm__ volatile("invlpg (%0)"::"r"(va):"memory");}static inline uint64_t read_cr0(void){uint64_t v;__asm__ volatile("mov %%cr0,%0":"=r"(v));return v;}static inline void write_cr0(uint64_t v){__asm__ volatile("mov %0,%%cr0"::"r"(v):"memory");}static inline uint64_t read_cr4(void){uint64_t v;__asm__ volatile("mov %%cr4,%0":"=r"(v));return v;}static inline void write_cr4(uint64_t v){__asm__ volatile("mov %0,%%cr4"::"r"(v):"memory");}
static inline uint64_t read_efer(void){uint32_t lo,hi;__asm__ volatile("rdmsr":"=a"(lo),"=d"(hi):"c"(EFER_MSR));return((uint64_t)hi<<32)|(uint64_t)lo;}
static inline void write_efer(uint64_t v){uint32_t lo=(uint32_t)v,hi=(uint32_t)(v>>32);__asm__ volatile("wrmsr"::"c"(EFER_MSR),"a"(lo),"d"(hi):"memory");}
static uint64_t *entry_table(uint64_t e){return(uint64_t *)(uintptr_t)(e&PAGE_MASK);}static int canonical48(uint64_t va){return va<(1ULL<<47)||va>=(UINT64_MAX-(1ULL<<47)+1ULL);}
static uint64_t *ensure_table(uint64_t *parent,size_t index,uint64_t flags){uint64_t e=parent[index];if(e&RIXURI_PTE_PRESENT){if(flags&RIXURI_PTE_USER)parent[index]|=RIXURI_PTE_USER;return entry_table(parent[index]);}uint64_t phys=pmm_alloc_page();if(!phys)return NULL;uint64_t*t=(uint64_t *)(uintptr_t)phys;for(size_t i=0;i<TABLE_ENTRIES;i++)t[i]=0;parent[index]=phys|RIXURI_PTE_PRESENT|RIXURI_PTE_WRITE|(flags&RIXURI_PTE_USER);return t;}
static uint64_t *split_pd_huge_page(uint64_t *pd,size_t index,uint64_t flags){uint64_t e=pd[index];if(!(e&RIXURI_PTE_PRESENT))return ensure_table(pd,index,flags);if(!(e&PTE_PS)){if(flags&RIXURI_PTE_USER)pd[index]|=RIXURI_PTE_USER;return entry_table(pd[index]);}uint64_t phys=pmm_alloc_page();if(!phys)return NULL;uint64_t*pt=(uint64_t *)(uintptr_t)phys;uint64_t base=e&PAGE_MASK;uint64_t leaf=e&(RIXURI_PTE_WRITE|RIXURI_PTE_USER|RIXURI_PTE_NX);for(size_t i=0;i<TABLE_ENTRIES;i++)pt[i]=(base+(uint64_t)i*0x1000ULL)|RIXURI_PTE_PRESENT|leaf;pd[index]=phys|RIXURI_PTE_PRESENT|RIXURI_PTE_WRITE|(e&RIXURI_PTE_USER)|(flags&RIXURI_PTE_USER);return pt;}
static void reserve_table_range(uint64_t start, size_t bytes){
 uint64_t first=start&~0xfffULL;
 uint64_t last=(start+(uint64_t)bytes+0xfffULL)&~0xfffULL;
 for(uint64_t page=first;page<last;page+=0x1000ULL)pmm_reserve_page(page);
}
extern uint8_t __text_start[], __text_end[], __rodata_start[], __rodata_end[];
extern uint8_t __data_start[], __bss_end[];

static int protect_kernel_range(uintptr_t start, uintptr_t end, uint64_t flags) {
    if (end <= start) return 0;
    start &= ~0xFFFULL;
    end = (end + 0xFFFULL) & ~0xFFFULL;
    for (uintptr_t va = start; va < end; va += 0x1000ULL)
        if (vmm_map_page_in_pml4(kernel_pml4_phys, va, va, flags) != 0) return -1;
    return 0;
}

static int protect_kernel_sections(void) {
    if (protect_kernel_range((uintptr_t)__text_start, (uintptr_t)__text_end,
                             RIXURI_PTE_PRESENT) != 0) return -1;
    if (protect_kernel_range((uintptr_t)__rodata_start, (uintptr_t)__rodata_end,
                             RIXURI_PTE_PRESENT | RIXURI_PTE_NX) != 0) return -1;
    if (protect_kernel_range((uintptr_t)__data_start, (uintptr_t)__bss_end,
                             RIXURI_PTE_PRESENT | RIXURI_PTE_WRITE | RIXURI_PTE_NX) != 0) return -1;
    return 0;
}

/* Report-and-verify that the protections actually landed (a silent remap
 * proves nothing). Queries the live tables: text must be R-X, rodata
 * R--, data RW-. Entry trampolines live in .text (see user_entry.S);
 * a MISMATCH here means code landed in a data section. */
static void verify_kernel_sections(void) {
    uint64_t text = vmm_query_flags((uint64_t)(uintptr_t)__text_start);
    uint64_t rodata = vmm_query_flags((uint64_t)(uintptr_t)__rodata_start);
    uint64_t data = vmm_query_flags((uint64_t)(uintptr_t)__data_start);
    int ok = (text & RIXURI_PTE_PRESENT) && !(text & (RIXURI_PTE_WRITE | RIXURI_PTE_NX | RIXURI_PTE_USER)) &&
             (rodata & (RIXURI_PTE_PRESENT | RIXURI_PTE_NX)) == (RIXURI_PTE_PRESENT | RIXURI_PTE_NX) &&
             !(rodata & (RIXURI_PTE_WRITE | RIXURI_PTE_USER)) &&
             (data & (RIXURI_PTE_PRESENT | RIXURI_PTE_WRITE | RIXURI_PTE_NX)) ==
             (RIXURI_PTE_PRESENT | RIXURI_PTE_WRITE | RIXURI_PTE_NX) && !(data & RIXURI_PTE_USER);
    serial_write(ok ? "VMM: section perms verified R-X/R--/RW-\r\n" : "VMM: section perms MISMATCH\r\n");
}

void vmm_early_init(void){rix_lockdep_register("vmm-map", 10u, &vmm_map_lockdep_class);for(size_t i=0;i<TABLE_ENTRIES;i++)early_pml4[i]=0;for(size_t n=0;n<IDENTITY_PML4_COUNT;n++){for(size_t i=0;i<TABLE_ENTRIES;i++)early_pdpt[n][i]=0;}for(size_t n=0;n<IDENTITY_PDPT_COUNT;n++)for(size_t i=0;i<TABLE_ENTRIES;i++)early_pd[n][i]=0;for(uint64_t p=0;p<IDENTITY_PML4_COUNT;p++){early_pml4[p]=(uint64_t)(uintptr_t)early_pdpt[p]|RIXURI_PTE_PRESENT|RIXURI_PTE_WRITE;for(uint64_t n=0;n<TABLE_ENTRIES;n++){uint64_t pd_index=p*TABLE_ENTRIES+n;early_pdpt[p][n]=(uint64_t)(uintptr_t)early_pd[pd_index]|RIXURI_PTE_PRESENT|RIXURI_PTE_WRITE;for(uint64_t i=0;i<TABLE_ENTRIES;i++){uint64_t pa=(pd_index*TABLE_ENTRIES+i)*0x200000ULL;early_pd[pd_index][i]=pa|RIXURI_PTE_PRESENT|RIXURI_PTE_WRITE|PTE_PS;}}}reserve_table_range((uint64_t)(uintptr_t)early_pml4,sizeof(early_pml4));reserve_table_range((uint64_t)(uintptr_t)early_pdpt,sizeof(early_pdpt));reserve_table_range((uint64_t)(uintptr_t)early_pd,sizeof(early_pd));kernel_pml4_phys=(uint64_t)(uintptr_t)early_pml4;current_pml4_phys=kernel_pml4_phys;if(protect_kernel_sections()!=0)serial_write("VMM: kernel section permissions unavailable\r\n");else verify_kernel_sections();uint64_t cr4=read_cr4()|CR4_PAE;/* SMEP/SMAP/PKE/PGE and PCIDE are not yet handled by the uaccess/TLB code. Firmware may leave them set on physical CPUs; clear them before the first user transition. PGE matters: with PGE=1 MOV CR3 keeps stale firmware global entries, so the first user CR3 on real AMI firmware can fetch through a stale global TLB entry while QEMU (PGE=0) works. */cr4&=~(CR4_LA57|CR4_PGE|(1ULL<<17)|(1ULL<<20)|(1ULL<<21)|(1ULL<<22));/* The kernel builds without -mno-sse, so GCC may emit SSE for copies/loops. OVMF leaves FXSR/XMMEXCPT on; physical firmware may not. Enable both and clear EM/TS so SSE never raises #UD/#NM on real silicon. QEMU-safe: already set there. */cr4|=CR4_OSFXSR|CR4_OSXMMEXCPT;write_cr4(cr4);uint64_t cr0=read_cr0()&~(CR0_EM|CR0_TS);write_cr0(cr0|CR0_WP);/* Every user mapping carries the NX bit; firmware is not required to leave EFER.NXE on (OVMF does, physical boards may not). Without it the first Ring-3 touch of an NX page raises #PF(RSVD). Enable unconditionally: already-set is a no-op. */write_efer(read_efer()|EFER_NXE);write_cr3(current_pml4_phys);}
uint64_t vmm_kernel_pml4(void){return kernel_pml4_phys;}
uint64_t vmm_current_pml4(void){return current_pml4_phys;}
void *vmm_phys_ptr(uint64_t physical_address){if(!physical_address||(physical_address&0xFFFULL)||physical_address>=RIXURI_MAX_PHYS_BYTES)return NULL;return(void *)(uintptr_t)physical_address;}
void vmm_switch_pml4(uint64_t pml4_phys){if(!pml4_phys)return;current_pml4_phys=pml4_phys;write_cr3(pml4_phys);}
void vmm_track_pml4(uint64_t pml4_phys){if(!pml4_phys)return;current_pml4_phys=pml4_phys;}
int vmm_map_page_in_pml4(uint64_t pml4_phys,uint64_t va,uint64_t pa,uint64_t flags){if(!pml4_phys||!canonical48(va)||(va&0xFFFULL)||(pa&0xFFFULL)||(pa&~PAGE_MASK))return -1;uint64_t irq;rix_spin_lock_irqsave(&vmm_map_lock,&irq);(void)rix_lockdep_acquire(vmm_map_lockdep_class);uint64_t*pml4=(uint64_t *)(uintptr_t)pml4_phys;uint64_t*pdpt=ensure_table(pml4,(va>>39)&0x1FFULL,flags);if(!pdpt){(void)rix_lockdep_release(vmm_map_lockdep_class);rix_spin_unlock_irqrestore(&vmm_map_lock,irq);return -1;}uint64_t*pd=ensure_table(pdpt,(va>>30)&0x1FFULL,flags);if(!pd){(void)rix_lockdep_release(vmm_map_lockdep_class);rix_spin_unlock_irqrestore(&vmm_map_lock,irq);return -1;}uint64_t*pt=split_pd_huge_page(pd,(va>>21)&0x1FFULL,flags);if(!pt){(void)rix_lockdep_release(vmm_map_lockdep_class);rix_spin_unlock_irqrestore(&vmm_map_lock,irq);return -1;}size_t idx=(size_t)((va>>12)&0x1FFULL);pt[idx]=(pa&PAGE_MASK)|(flags&LEAF_FLAGS);if(pml4_phys==current_pml4_phys)invlpg(va);(void)rix_lockdep_release(vmm_map_lockdep_class);rix_spin_unlock_irqrestore(&vmm_map_lock,irq);return 0;}
int vmm_unmap_page_in_pml4(uint64_t pml4_phys,uint64_t va){if(!pml4_phys||!canonical48(va)||(va&0xFFFULL))return -1;uint64_t irq;rix_spin_lock_irqsave(&vmm_map_lock,&irq);(void)rix_lockdep_acquire(vmm_map_lockdep_class);uint64_t*pml4=(uint64_t *)(uintptr_t)pml4_phys;uint64_t e=pml4[(va>>39)&0x1FFULL];if(!(e&RIXURI_PTE_PRESENT)){(void)rix_lockdep_release(vmm_map_lockdep_class);rix_spin_unlock_irqrestore(&vmm_map_lock,irq);return 0;}uint64_t*pdpt=entry_table(e);e=pdpt[(va>>30)&0x1FFULL];if(!(e&RIXURI_PTE_PRESENT)){(void)rix_lockdep_release(vmm_map_lockdep_class);rix_spin_unlock_irqrestore(&vmm_map_lock,irq);return 0;}uint64_t*pd=entry_table(e);e=pd[(va>>21)&0x1FFULL];if(!(e&RIXURI_PTE_PRESENT)||(e&PTE_PS)){(void)rix_lockdep_release(vmm_map_lockdep_class);rix_spin_unlock_irqrestore(&vmm_map_lock,irq);return 0;}uint64_t*pt=entry_table(e);pt[(va>>12)&0x1FFULL]=0;if(pml4_phys==current_pml4_phys)invlpg(va);(void)rix_lockdep_release(vmm_map_lockdep_class);rix_spin_unlock_irqrestore(&vmm_map_lock,irq);return 0;}
/* NOTE (Phase D2): this flushes only the local TLB. Unmapping a page that
 * other CPUs may hold (shared kernel mappings) additionally requires
 * smp_shootdown(va); see address_space_unmap for the established pattern.
 * No in-tree caller unmaps shared kernel pages today. */
int vmm_map_page(uint64_t va,uint64_t pa,uint64_t flags){return vmm_map_page_in_pml4(current_pml4_phys,va,pa,flags);}
void vmm_unmap_page(uint64_t va){(void)vmm_unmap_page_in_pml4(current_pml4_phys,va);}
void vmm_invlpg(uint64_t va){if(!canonical48(va)||(va&0xFFFULL))return;invlpg(va);}
#define MMIO_PTE_FLAGS (RIXURI_PTE_PRESENT|RIXURI_PTE_WRITE|RIXURI_PTE_NX|RIXURI_PTE_PWT|RIXURI_PTE_PCD)
#define MMIO_WINDOW_BASE 0xFFFF960000000000ULL
#define MMIO_WINDOW_PAGES 16384ULL
#define MMIO_REG_MAX 32u
static uint64_t mmio_window_next;
static struct { uint64_t phys, va, pages; } mmio_regs[MMIO_REG_MAX];
static unsigned mmio_reg_count;
static int mmio_shared_slot(unsigned idx){return idx==0||idx==2||idx==3||idx>=256;}
static int mmio_va_registered(uint64_t va){for(unsigned i=0;i<mmio_reg_count;i++){if(va>=mmio_regs[i].va&&va-mmio_regs[i].va<mmio_regs[i].pages*0x1000ULL)return 1;}return 0;}
uint64_t vmm_map_mmio(uint64_t pa,uint64_t size){
 if(!pa||!size||size-1u>UINT64_MAX-0x1000ULL)return 0;
 uint64_t base=pa&~0xFFFULL,off=pa&0xFFFULL;
 if(off>UINT64_MAX-size)return 0;
 uint64_t end=off+size;
 if(end>UINT64_MAX-0xFFFULL||(end+0xFFFULL<=end))return 0;
 uint64_t pages=(end+0xFFFULL)>>12;if(!pages||pages>MMIO_WINDOW_PAGES)return 0;
 uint64_t va=0;
 for(unsigned i=0;i<mmio_reg_count;i++)if(mmio_regs[i].phys==base&&mmio_regs[i].pages==pages){va=mmio_regs[i].va;break;}
 if(!va){
  if(mmio_reg_count>=MMIO_REG_MAX)return 0;
  unsigned idx=(unsigned)((base>>39)&0x1FFULL);
  if(mmio_shared_slot(idx))va=base;
  else{
   if(!mmio_window_next)mmio_window_next=MMIO_WINDOW_BASE;
   if(mmio_window_next-MMIO_WINDOW_BASE>(MMIO_WINDOW_PAGES-pages)*0x1000ULL)return 0;
   va=mmio_window_next;mmio_window_next+=pages*0x1000ULL;
  }
  for(uint64_t p=0;p<pages;p++)if(vmm_map_page_in_pml4(kernel_pml4_phys,va+p*0x1000ULL,base+p*0x1000ULL,MMIO_PTE_FLAGS)!=0)return 0;
  mmio_regs[mmio_reg_count].phys=base;mmio_regs[mmio_reg_count].va=va;mmio_regs[mmio_reg_count].pages=pages;mmio_reg_count++;
 }
 if(current_pml4_phys!=kernel_pml4_phys){
  unsigned s=(unsigned)((va>>39)&0x1FFULL);
  uint64_t*kp=(uint64_t*)(uintptr_t)kernel_pml4_phys,*cp=(uint64_t*)(uintptr_t)current_pml4_phys;
  if(!kp||!cp)return 0;
  cp[s]=kp[s];
 }
 return va+off;
}
uint64_t vmm_translate(uint64_t va){if(!canonical48(va))return 0;uint64_t*pml4=(uint64_t *)(uintptr_t)current_pml4_phys;uint64_t e=pml4[(va>>39)&0x1FFULL];if(!(e&RIXURI_PTE_PRESENT))return 0;uint64_t*pdpt=entry_table(e);e=pdpt[(va>>30)&0x1FFULL];if(!(e&RIXURI_PTE_PRESENT))return 0;uint64_t*pd=entry_table(e);e=pd[(va>>21)&0x1FFULL];if(!(e&RIXURI_PTE_PRESENT))return 0;if(e&PTE_PS)return(e&PAGE_MASK)+(va&0x1FFFFFULL);uint64_t*pt=entry_table(e);e=pt[(va>>12)&0x1FFULL];if(!(e&RIXURI_PTE_PRESENT))return 0;return(e&PAGE_MASK)+(va&0xFFFULL);}
uint64_t vmm_query_flags(uint64_t va){if(!canonical48(va))return 0;uint64_t*pml4=(uint64_t *)(uintptr_t)current_pml4_phys;uint64_t e=pml4[(va>>39)&0x1FFULL];if(!(e&RIXURI_PTE_PRESENT))return 0;uint64_t*pdpt=entry_table(e);e=pdpt[(va>>30)&0x1FFULL];if(!(e&RIXURI_PTE_PRESENT))return 0;uint64_t*pd=entry_table(e);e=pd[(va>>21)&0x1FFULL];if(!(e&RIXURI_PTE_PRESENT))return 0;if(e&PTE_PS)return e&(RIXURI_PTE_PRESENT|RIXURI_PTE_WRITE|RIXURI_PTE_USER|RIXURI_PTE_NX);uint64_t*pt=entry_table(e);e=pt[(va>>12)&0x1FFULL];return e&(RIXURI_PTE_PRESENT|RIXURI_PTE_WRITE|RIXURI_PTE_USER|RIXURI_PTE_NX|RIXURI_PTE_OWNED);}
/* Target-PML4 validation before any CR3 write. Walk every reachable table,
 * reject reserved bits and verify huge-page alignment. Cycle protection is
 * path-based: at most one entry per live recursion level (depth never
 * exceeds PML4 L0 -> PDPT L1 -> PD L2 -> PT L3). A repeat on the current
 * path is a genuine cycle and fails closed; tables shared through sibling
 * slots are simply re-walked after the pop. This keeps the validator at a
 * few bytes of stack. (A 4096-entry visited set used to live here: 32 KiB
 * on the stack overflowed the 16 KiB task stacks and the 32 KiB boot stack,
 * spraying page-table words across neighboring .bss such as tasks[] --
 * silent on QEMU, wild context switches and #UD on real silicon. Do not
 * put a large visited set back on the stack; .bss or path-bounded.) */
#define PT_ALLOWED_FLAGS (RIXURI_PTE_PRESENT|RIXURI_PTE_WRITE|RIXURI_PTE_USER|RIXURI_PTE_PWT|RIXURI_PTE_PCD|(1ULL<<5)|(1ULL<<6)|(1ULL<<7)|(1ULL<<8)|RIXURI_PTE_OWNED|RIXURI_PTE_NX)
#define PT_PHYS_MASK 0x000FFFFFFFFFF000ULL
struct pt_validation_seen { uint64_t phys[8]; size_t count; };
static int pt_validate_table(uint64_t phys,unsigned level,uint64_t va,struct pt_validation_seen *seen){
 if(!phys||(phys&0xfffULL)||phys>=RIXURI_MAX_PHYS_BYTES||level>3)return -1;
 for(size_t n=0;n<seen->count;n++)if(seen->phys[n]==phys)return -8;
 if(seen->count>=sizeof(seen->phys)/sizeof(seen->phys[0]))return -2;
 seen->phys[seen->count++]=phys;
 uint64_t *table=(uint64_t *)vmm_phys_ptr(phys);if(!table){seen->count--;return -3;}
 for(size_t i=0;i<TABLE_ENTRIES;i++){
  uint64_t e=table[i];if(!(e&RIXURI_PTE_PRESENT))continue;
  if(e&~(PT_PHYS_MASK|PT_ALLOWED_FLAGS)){seen->count--;return -4;}
  uint64_t base=e&PT_PHYS_MASK;
  if(level==1&&(e&PTE_PS)){if(base&((1ULL<<30)-1ULL)){seen->count--;return -6;}continue;}
  if(level==2&&(e&PTE_PS)){if(base&((1ULL<<21)-1ULL)){seen->count--;return -7;}continue;}
  if(!base||base>=RIXURI_MAX_PHYS_BYTES){
   /* Registered supervisor-uncached MMIO leaves live above RAM by design
    * (high BARs served from the shared MMIO window). Anything else,
    * including unregistered or user-marked high leaves, fails closed. */
   if(level==3&&base&&!(e&RIXURI_PTE_USER)&&(e&(RIXURI_PTE_PWT|RIXURI_PTE_PCD))==(RIXURI_PTE_PWT|RIXURI_PTE_PCD)&&mmio_va_registered(va+(uint64_t)i*0x1000ULL)){continue;}
   seen->count--;return -5;
  }
  if(level==3)continue;
  /* Child table VA: stride is 1 GiB/2 MiB/4 KiB per level; PML4 slots
   * >=256 live in the high canonical half and need sign extension. */
  uint64_t child_va=va+((uint64_t)i<<(39u-9u*level));
  if(level==0&&(i&0x100u))child_va|=0xFFFF000000000000ULL;
  int rc=pt_validate_table(base,level+1,child_va,seen);if(rc){seen->count--;return rc;}
 }
 seen->count--;
 return 0;
}
int vmm_validate_pml4(uint64_t pml4_phys){
 if(!pml4_phys||(pml4_phys&0xfffULL)||pml4_phys>=RIXURI_MAX_PHYS_BYTES)return -1;
 if(pml4_phys&0xfff0000000000000ULL)return -2;
 uint64_t *pml4=(uint64_t *)vmm_phys_ptr(pml4_phys);if(!pml4)return -3;
 if(!(pml4[0]&RIXURI_PTE_PRESENT))return -4;
 struct pt_validation_seen seen;seen.count=0;return pt_validate_table(pml4_phys,0,0,&seen);
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
 /* Keep the forensic walk visible on physical-console-only machines. */
 kernel_log("WALK ");
 kernel_log(label?label:"?");
 kernel_log(" va="); kernel_log_hex(va);
 kernel_log(" rc="); kernel_log_dec((uint64_t)(rc<0?UINT64_MAX:(uint64_t)rc));
 kernel_log(rc==0?" MAPPED\r\n":" UNMAPPED\r\n");
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
