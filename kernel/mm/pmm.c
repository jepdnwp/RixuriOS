#include "pmm.h"
#include <stddef.h>
#include <stdint.h>

#define PMM_MAX_PHYS RIXURI_MAX_PHYS_BYTES
#define PMM_MAX_PAGES RIXURI_MAX_PAGES
#define EFI_DESCRIPTOR_MIN_SIZE 40ULL

/* page_bitmap: 1 = unavailable/allocated, 0 = free. */
static uint64_t page_bitmap[RIXURI_BITMAP_WORDS];
/* managed_bitmap: 1 = page belongs to firmware-reported usable memory. */
static uint64_t managed_bitmap[RIXURI_BITMAP_WORDS];
static uint64_t reserved_bitmap[RIXURI_BITMAP_WORDS];
static uint64_t total_pages_count;
static uint64_t free_pages_count;
/* Saved UEFI map for post-init region queries (buffer is reserved at init
 * so it stays intact; identity-mapped low memory, readable under any CR3
 * that shares the low identity window). */
static const unsigned char *saved_map;
static uint64_t saved_map_size;
static uint64_t saved_desc_size;

static int usable_type(uint32_t type) { return type == 1 || type == 2 || type == 3 || type == 4 || type == 7; }
static uint64_t align_up_page(uint64_t value) { if(value>UINT64_MAX-(RIXURI_PAGE_SIZE-1ULL))return UINT64_MAX;return(value+RIXURI_PAGE_SIZE-1ULL)&~(RIXURI_PAGE_SIZE-1ULL); }
static void mark_range(uint64_t base,uint64_t pages,int freeable){
    if(!pages||base>=PMM_MAX_PHYS)return;
    uint64_t first_addr=align_up_page(base);
    if(first_addr==UINT64_MAX||first_addr>=PMM_MAX_PHYS)return;
    uint64_t first=first_addr/RIXURI_PAGE_SIZE;
    uint64_t max_pages=(PMM_MAX_PHYS-first_addr)/RIXURI_PAGE_SIZE;
    if(pages>max_pages)pages=max_pages;
    uint64_t last=first+pages;
    for(uint64_t p=first;p<last;p++){
        uint64_t word_index=p>>6, bit=1ULL<<(p&63ULL);
        if(freeable){
            if(!(managed_bitmap[word_index]&bit)){managed_bitmap[word_index]|=bit;++total_pages_count;}
            if(page_bitmap[word_index]&bit){page_bitmap[word_index]&=~bit;++free_pages_count;}
        }else{
            /* Firmware-usable is not the same as allocator-releasable.
             * Kernel image, boot metadata, the low legacy window and the
             * saved memory map must remain permanently owned by the boot
             * environment even if a caller later invokes pmm_free_page(). */
            reserved_bitmap[word_index]|=bit;
            if(!(page_bitmap[word_index]&bit)){
                page_bitmap[word_index]|=bit;
                if(free_pages_count)--free_pages_count;
            }
        }
    }
}
void pmm_init(const void*memory_map,uint64_t memory_map_size,uint64_t descriptor_size,uint64_t kernel_base,uint64_t kernel_end,uint64_t boot_info,uint64_t boot_info_size){
    for(size_t i=0;i<RIXURI_BITMAP_WORDS;i++){page_bitmap[i]=UINT64_MAX;managed_bitmap[i]=0;reserved_bitmap[i]=0;}
    total_pages_count=free_pages_count=0;
    if(!memory_map||descriptor_size<EFI_DESCRIPTOR_MIN_SIZE||descriptor_size>4096||memory_map_size<descriptor_size)return;
    saved_map=(const unsigned char*)memory_map;saved_map_size=memory_map_size;saved_desc_size=descriptor_size;
    uint64_t offset=0;
    while(offset<=memory_map_size-descriptor_size){
        const unsigned char*d=(const unsigned char*)memory_map+offset;
        uint32_t type;uint64_t base,pages;
        __builtin_memcpy(&type,d,sizeof(type));
        __builtin_memcpy(&base,d+8,sizeof(base));
        __builtin_memcpy(&pages,d+24,sizeof(pages));
        if(usable_type(type)&&pages&&base<PMM_MAX_PHYS){
            uint64_t max_pages=(PMM_MAX_PHYS-base)/RIXURI_PAGE_SIZE;
            if(pages>max_pages)pages=max_pages;
            mark_range(base,pages,1);
        }
        offset+=descriptor_size;
    }
    /* Never hand the low legacy region or boot metadata to the allocator. */
    mark_range(0,0x100000ULL/RIXURI_PAGE_SIZE,0);
    if(kernel_end>kernel_base)mark_range(kernel_base,(kernel_end-kernel_base+RIXURI_PAGE_SIZE-1ULL)/RIXURI_PAGE_SIZE,0);
    if(boot_info_size)mark_range(boot_info,(boot_info_size+RIXURI_PAGE_SIZE-1ULL)/RIXURI_PAGE_SIZE,0);
    mark_range((uint64_t)(uintptr_t)memory_map,(memory_map_size+RIXURI_PAGE_SIZE-1ULL)/RIXURI_PAGE_SIZE,0);
}
uint64_t pmm_alloc_page_below(uint64_t max_exclusive){
    if(max_exclusive>PMM_MAX_PHYS)max_exclusive=PMM_MAX_PHYS;
    if(max_exclusive<RIXURI_PAGE_SIZE)return 0;
    uint64_t limit=(max_exclusive-1ULL)/RIXURI_PAGE_SIZE;
    for(uint64_t w=0;w<RIXURI_BITMAP_WORDS;w++){
        uint64_t first=w*64ULL;if(first>limit)break;
        uint64_t candidates=managed_bitmap[w]&~page_bitmap[w];
        if(w==(limit>>6)){uint64_t mask=((limit&63ULL)==63ULL)?UINT64_MAX:((1ULL<<((limit&63ULL)+1ULL))-1ULL);candidates&=mask;}
        if(!candidates)continue;
        unsigned bit=(unsigned)__builtin_ctzll(candidates);uint64_t page=first+bit;
        if(page>=PMM_MAX_PAGES||page>limit)continue;
        page_bitmap[w]|=1ULL<<bit;if(free_pages_count)--free_pages_count;return page*RIXURI_PAGE_SIZE;
    }
    return 0;
}
uint64_t pmm_alloc_page(void){return pmm_alloc_page_below(PMM_MAX_PHYS);}
uint64_t pmm_alloc_pages(size_t count){
    if(!count||count>PMM_MAX_PAGES)return 0;
    for(uint64_t start=0;start+count<=PMM_MAX_PAGES;start++){
        int available=1;
        for(size_t i=0;i<count;i++){
            uint64_t page=start+(uint64_t)i,word=page>>6,bit=1ULL<<(page&63ULL);
            if(!(managed_bitmap[word]&bit)||page_bitmap[word]&bit){available=0;break;}
        }
        if(!available)continue;
        for(size_t i=0;i<count;i++){
            uint64_t page=start+(uint64_t)i,word=page>>6,bit=1ULL<<(page&63ULL);
            page_bitmap[word]|=bit;
        }
        free_pages_count-=count;
        return start*RIXURI_PAGE_SIZE;
    }
    return 0;
}
void pmm_free_page_range(uint64_t physical_address,size_t count){
    if(!count||(physical_address&(RIXURI_PAGE_SIZE-1ULL))!=0)return;
    for(size_t i=0;i<count;i++)pmm_free_page(physical_address+(uint64_t)i*RIXURI_PAGE_SIZE);
}
void pmm_reserve_page(uint64_t physical_address){
    if((physical_address&(RIXURI_PAGE_SIZE-1ULL))!=0)return;
    uint64_t page=physical_address/RIXURI_PAGE_SIZE;if(page>=PMM_MAX_PAGES)return;
    uint64_t*managed=&managed_bitmap[page>>6],*used=&page_bitmap[page>>6],*reserved=&reserved_bitmap[page>>6],bit=1ULL<<(page&63ULL);
    if(!(*managed&bit))return;
    *reserved |= bit;
    if(!(*used&bit)){*used|=bit;if(free_pages_count)--free_pages_count;}
}
void pmm_free_page(uint64_t physical_address){
    if((physical_address&(RIXURI_PAGE_SIZE-1ULL))!=0)return;
    uint64_t page=physical_address/RIXURI_PAGE_SIZE;if(page>=PMM_MAX_PAGES)return;
    uint64_t*managed=&managed_bitmap[page>>6],*used=&page_bitmap[page>>6],*reserved=&reserved_bitmap[page>>6],bit=1ULL<<(page&63ULL);
    if(!(*managed&bit)||!(*used&bit)||(*reserved&bit))return;
    *used&=~bit;++free_pages_count;
}
uint64_t pmm_total_pages(void){return total_pages_count;}
uint64_t pmm_free_pages(void){return free_pages_count;}
int pmm_is_managed(uint64_t physical_address){
    if((physical_address&(RIXURI_PAGE_SIZE-1ULL))!=0)return 0;
    uint64_t page=physical_address/RIXURI_PAGE_SIZE;if(page>=PMM_MAX_PAGES)return 0;
    return (managed_bitmap[page>>6]&(1ULL<<(page&63ULL)))!=0;
}
int pmm_is_in_use(uint64_t physical_address){
    if((physical_address&(RIXURI_PAGE_SIZE-1ULL))!=0)return 0;
    uint64_t page=physical_address/RIXURI_PAGE_SIZE;if(page>=PMM_MAX_PAGES)return 0;
    return (page_bitmap[page>>6]&(1ULL<<(page&63ULL)))!=0;
}
int pmm_is_reserved(uint64_t physical_address){
    if((physical_address&(RIXURI_PAGE_SIZE-1ULL))!=0)return 0;
    uint64_t page=physical_address/RIXURI_PAGE_SIZE;if(page>=PMM_MAX_PAGES)return 0;
    return (reserved_bitmap[page>>6]&(1ULL<<(page&63ULL)))!=0;
}
int pmm_region_info(uint64_t physical_address,uint64_t *out_base,uint64_t *out_end,uint32_t *out_type,int *out_usable){
    if(out_base){*out_base=0;}if(out_end){*out_end=0;}if(out_type){*out_type=0;}if(out_usable){*out_usable=0;}
    if(!saved_map||saved_desc_size<EFI_DESCRIPTOR_MIN_SIZE||saved_desc_size>4096||saved_map_size<saved_desc_size)return -1;
    uint64_t offset=0;
    while(offset<=saved_map_size-saved_desc_size){
        const unsigned char*d=saved_map+offset;
        uint32_t type;uint64_t base,pages;
        __builtin_memcpy(&type,d,sizeof(type));
        __builtin_memcpy(&base,d+8,sizeof(base));
        __builtin_memcpy(&pages,d+24,sizeof(pages));
        if(pages&&base<PMM_MAX_PHYS&&physical_address>=base){
            uint64_t span=pages*RIXURI_PAGE_SIZE;
            if(span/RIXURI_PAGE_SIZE==pages&&physical_address<base+span){
                if(out_base){*out_base=base;}if(out_end){*out_end=base+span;}
                if(out_type){*out_type=type;}if(out_usable){*out_usable=usable_type(type);}
                return 0;
            }
        }
        offset+=saved_desc_size;
    }
    return 1;
}
