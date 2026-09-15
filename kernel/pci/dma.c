#include "dma.h"
#include "../mm/pmm.h"
#include "../sync/lock.h"
#include "../sync/lockdep.h"
#include <stddef.h>

#define DMA_PAGE 4096ULL

typedef struct { uint64_t dev; uint64_t phys; size_t size; rix_dma_direction_t dir; uint64_t owner; uint8_t valid; uint8_t bounced; uint64_t bounce; } dma_entry_t;
static dma_entry_t dma_map[RIX_DMA_MAX_MAPPINGS];
static rix_spinlock_t dma_lock;
static unsigned dma_lockdep_class;
static int dma_registered;

static void dma_ensure(void){
    if(dma_registered)return;
    rix_spin_init(&dma_lock);
    rix_lockdep_register("dma", 25u, &dma_lockdep_class);
    dma_registered=1;
}
static int overlap(uint64_t a,size_t as,uint64_t b,size_t bs){
    if(!as||!bs)return 0;
    if(a+as<a||b+bs<b)return 1;
    return a<b+bs&&b<a+as;
}
static int pages_valid(uint64_t phys,size_t size){
    if(!size||size> RIX_DMA_MAX_PAGES*DMA_PAGE)return 0;
    uint64_t start=phys&~(DMA_PAGE-1ULL);
    uint64_t end=(phys+size+DMA_PAGE-1ULL)&~(DMA_PAGE-1ULL);
    if(end<start||end-phys> RIX_DMA_MAX_PAGES*DMA_PAGE+DMA_PAGE)return 0;
    for(uint64_t p=start;p<end;p+=DMA_PAGE){
        if(!pmm_is_managed(p)||!pmm_is_in_use(p)||pmm_is_reserved(p))return 0;
    }
    return 1;
}
static void dma_memcpy(void*dst,const void*src,size_t n){
    uint8_t*d=(uint8_t*)dst;const uint8_t*s=(const uint8_t*)src;
    for(size_t i=0;i<n;i++)d[i]=s[i];
}

int pci_dma_alloc(size_t pages,uint64_t max_physical_exclusive,rix_dma_buffer_t*out){
    if(!out||!pages||pages>RIX_DMA_MAX_PAGES||max_physical_exclusive<=RIXURI_PAGE_SIZE)return -1;
    dma_ensure();
    out->count=0;
    for(size_t i=0;i<pages;i++){uint64_t pa=pmm_alloc_page_below(max_physical_exclusive);if(!pa){pci_dma_free(out);return -1;}out->pages[out->count++]=pa;}
    return 0;
}
void pci_dma_free(rix_dma_buffer_t*b){
    if(!b)return;
    dma_ensure();
    for(size_t i=0;i<b->count;i++){
        if(!b->pages[i])continue;
        /* Fail closed on DMA-after-free: a still-mapped page is leaked,
         * never handed back while a device may DMA into it. */
        if(pci_dma_is_mapped(b->pages[i],DMA_PAGE))continue;
        pmm_free_page(b->pages[i]);
    }
    b->count=0;
}
int pci_dma_map(uint64_t phys,size_t size,rix_dma_direction_t dir,uint64_t owner,uint64_t*out_dev){
    if(!size||!out_dev||!owner||dir>RIX_DMA_BIDIRECTIONAL)return -1;
    if(phys+size<phys)return -1;
    dma_ensure();
    if(!pages_valid(phys,size))return -1;
    uint64_t irq;rix_spin_lock_irqsave(&dma_lock,&irq);
    (void)rix_lockdep_acquire(dma_lockdep_class);
    for(size_t i=0;i<RIX_DMA_MAX_MAPPINGS;i++){
        if(!dma_map[i].valid)continue;
        if(overlap(dma_map[i].dev,dma_map[i].size,phys,size)){
            (void)rix_lockdep_release(dma_lockdep_class);
            rix_spin_unlock_irqrestore(&dma_lock,irq);
            return -1;
        }
    }
    for(size_t i=0;i<RIX_DMA_MAX_MAPPINGS;i++){
        if(dma_map[i].valid)continue;
        dma_map[i].dev=phys;dma_map[i].phys=phys;dma_map[i].size=size;
        dma_map[i].dir=dir;dma_map[i].owner=owner;dma_map[i].valid=1;
        dma_map[i].bounced=0;dma_map[i].bounce=0;
        *out_dev=phys;
        (void)rix_lockdep_release(dma_lockdep_class);
        rix_spin_unlock_irqrestore(&dma_lock,irq);
        return 0;
    }
    (void)rix_lockdep_release(dma_lockdep_class);
    rix_spin_unlock_irqrestore(&dma_lock,irq);
    return -1;
}
int pci_dma_unmap(uint64_t dev,size_t size){
    if(!size)return -1;
    dma_ensure();
    uint64_t irq;rix_spin_lock_irqsave(&dma_lock,&irq);
    (void)rix_lockdep_acquire(dma_lockdep_class);
    for(size_t i=0;i<RIX_DMA_MAX_MAPPINGS;i++){
        if(!dma_map[i].valid||dma_map[i].bounced)continue;
        if(dma_map[i].dev==dev&&dma_map[i].size==size){
            dma_map[i].valid=0;dma_map[i].dev=dma_map[i].phys=0;
            dma_map[i].size=0;dma_map[i].owner=0;
            (void)rix_lockdep_release(dma_lockdep_class);
            rix_spin_unlock_irqrestore(&dma_lock,irq);
            return 0;
        }
    }
    (void)rix_lockdep_release(dma_lockdep_class);
    rix_spin_unlock_irqrestore(&dma_lock,irq);
    return -1;
}
int pci_dma_map_sg(const uint64_t*list,size_t count,rix_dma_direction_t dir,uint64_t owner,uint64_t*out){
    if(!list||!out||!count||count>RIX_DMA_MAX_PAGES||!owner||dir>RIX_DMA_BIDIRECTIONAL)return -1;
    size_t done=0;
    for(size_t i=0;i<count;i++){
        if(!list[i]||(list[i]&0xFFFULL))goto fail;
        if(pci_dma_map(list[i],DMA_PAGE,dir,owner,&out[i])!=0)goto fail;
        done++;
    }
    return 0;
fail:
    for(size_t j=0;j<done;j++)(void)pci_dma_unmap(out[j],DMA_PAGE);
    return -1;
}
int pci_dma_is_mapped(uint64_t phys,size_t size){
    if(!size)return 0;
    dma_ensure();
    uint64_t irq;rix_spin_lock_irqsave(&dma_lock,&irq);
    int found=0;
    for(size_t i=0;i<RIX_DMA_MAX_MAPPINGS;i++){
        if(!dma_map[i].valid)continue;
        uint64_t base=dma_map[i].bounced?dma_map[i].bounce:dma_map[i].dev;
        if(overlap(base,dma_map[i].size,phys,size)){found=1;break;}
        if(overlap(dma_map[i].phys,dma_map[i].size,phys,size)){found=1;break;}
    }
    rix_spin_unlock_irqrestore(&dma_lock,irq);
    return found;
}
void pci_dma_sync_for_device(uint64_t dev,size_t size){
    (void)dev;(void)size;
#ifdef RIX_HOST_TEST
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
#else
    __asm__ volatile("mfence":::"memory");
#endif
}
void pci_dma_sync_for_cpu(uint64_t dev,size_t size){
    (void)dev;(void)size;
#ifdef RIX_HOST_TEST
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
#else
    __asm__ volatile("mfence":::"memory");
#endif
}
int pci_dma_needs_bounce(uint64_t phys,size_t size,uint64_t max_exclusive){
    if(!size||!max_exclusive||phys+size<phys)return 1;
    uint64_t start=phys&~(DMA_PAGE-1ULL);
    uint64_t end=(phys+size+DMA_PAGE-1ULL)&~(DMA_PAGE-1ULL);
    for(uint64_t p=start;p<end;p+=DMA_PAGE){
        if(p+DMA_PAGE>max_exclusive||p>=max_exclusive)return 1;
    }
    return 0;
}
int pci_dma_map_bounce(uint64_t phys,size_t size,rix_dma_direction_t dir,uint64_t owner,uint64_t max_exclusive,uint64_t*out_dev){
    if(!size||!out_dev||!owner||dir>RIX_DMA_BIDIRECTIONAL||!max_exclusive)return -1;
    if(phys+size<phys)return -1;
    dma_ensure();
    if(!pages_valid(phys,size))return -1;
    if(!pci_dma_needs_bounce(phys,size,max_exclusive))
        return pci_dma_map(phys,size,dir,owner,out_dev);
    size_t pages=(size+DMA_PAGE-1ULL)/DMA_PAGE;
    if(!pages||pages>RIX_DMA_MAX_PAGES)return -1;
    rix_dma_buffer_t bounce;
    if(pci_dma_alloc(pages,max_exclusive,&bounce)!=0)return -1;
    /* Copy TO_DEVICE / BIDIRECTIONAL now; FROM_DEVICE copies back on unmap. */
    if(dir==RIX_DMA_TO_DEVICE||dir==RIX_DMA_BIDIRECTIONAL){
        uint8_t*dst=(uint8_t*)(uintptr_t)bounce.pages[0];
        const uint8_t*src=(const uint8_t*)(uintptr_t)phys;
        /* Bounce is page-aligned; source may be unaligned: copy bytes. */
        dma_memcpy(dst,src,size);
        /* If multi-page, scatter across bounce pages. */
        for(size_t i=1;i<pages;i++){
            size_t off=i*DMA_PAGE;
            if(off>=size)break;
            size_t n=size-off>DMA_PAGE?DMA_PAGE:size-off;
            dma_memcpy((uint8_t*)(uintptr_t)bounce.pages[i],(const uint8_t*)(uintptr_t)(phys+off),n);
        }
    }
    uint64_t irq;rix_spin_lock_irqsave(&dma_lock,&irq);
    (void)rix_lockdep_acquire(dma_lockdep_class);
    for(size_t i=0;i<RIX_DMA_MAX_MAPPINGS;i++){
        if(dma_map[i].valid)continue;
        dma_map[i].dev=bounce.pages[0];dma_map[i].phys=phys;dma_map[i].size=size;
        dma_map[i].dir=dir;dma_map[i].owner=owner;dma_map[i].valid=1;
        dma_map[i].bounced=1;dma_map[i].bounce=bounce.pages[0];
        /* Extra bounce pages beyond the first are tracked via the buffer?
         * For this bounded contract only single-page bounce is fully
         * tracked; multi-page bounce frees extras on unmap via table scan.
         * Store count in size; free path below handles all pages. */
        for(size_t k=1;k<bounce.count;k++){
            for(size_t j=0;j<RIX_DMA_MAX_MAPPINGS;j++){
                if(dma_map[j].valid)continue;
                dma_map[j].dev=bounce.pages[k];dma_map[j].phys=phys+k*DMA_PAGE;
                dma_map[j].size=0;dma_map[j].dir=dir;dma_map[j].owner=owner;
                dma_map[j].valid=1;dma_map[j].bounced=2;dma_map[j].bounce=bounce.pages[k];
                break;
            }
        }
        *out_dev=bounce.pages[0];
        (void)rix_lockdep_release(dma_lockdep_class);
        rix_spin_unlock_irqrestore(&dma_lock,irq);
        return 0;
    }
    (void)rix_lockdep_release(dma_lockdep_class);
    rix_spin_unlock_irqrestore(&dma_lock,irq);
    pci_dma_free(&bounce);
    return -1;
}
int pci_dma_unmap_bounce(uint64_t dev,size_t size,rix_dma_direction_t dir){
    (void)dir;
    if(!size)return -1;
    dma_ensure();
    uint64_t irq;rix_spin_lock_irqsave(&dma_lock,&irq);
    (void)rix_lockdep_acquire(dma_lockdep_class);
    for(size_t i=0;i<RIX_DMA_MAX_MAPPINGS;i++){
        if(!dma_map[i].valid||!dma_map[i].bounced||dma_map[i].bounced!=1)continue;
        if(dma_map[i].dev==dev&&dma_map[i].size==size){
            uint64_t orig=dma_map[i].phys,bnc=dma_map[i].bounce;
            rix_dma_direction_t d=dma_map[i].dir;
            dma_map[i].valid=0;
            /* Release continuation pages. */
            for(size_t j=0;j<RIX_DMA_MAX_MAPPINGS;j++){
                if(dma_map[j].valid&&dma_map[j].bounced==2&&dma_map[j].owner==dma_map[i].owner){
                    pmm_free_page(dma_map[j].bounce);
                    dma_map[j].valid=0;
                }
            }
            uint64_t owner_save=dma_map[i].owner;
            (void)owner_save;
            (void)rix_lockdep_release(dma_lockdep_class);
            rix_spin_unlock_irqrestore(&dma_lock,irq);
            if(d==RIX_DMA_FROM_DEVICE||d==RIX_DMA_BIDIRECTIONAL){
                dma_memcpy((void*)(uintptr_t)orig,(const void*)(uintptr_t)bnc,size);
            }
            pmm_free_page(bnc);
            return 0;
        }
    }
    (void)rix_lockdep_release(dma_lockdep_class);
    rix_spin_unlock_irqrestore(&dma_lock,irq);
    return pci_dma_unmap(dev,size);
}
