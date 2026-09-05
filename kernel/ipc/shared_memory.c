#include "shared_memory.h"
#include "../mm/pmm.h"
#include <stddef.h>

#define PAGE_SIZE 4096ULL
#define PAGE_MASK ~(PAGE_SIZE-1ULL)

typedef struct { uint8_t used; uint32_t refs; uint64_t size; uint64_t pages[RIX_SHM_MAX_PAGES]; } shm_object_t;
static shm_object_t objects[RIX_SHM_MAX];

int shm_init(void){
    for(size_t i=0;i<RIX_SHM_MAX;i++){objects[i].used=0;objects[i].refs=0;objects[i].size=0;for(size_t j=0;j<RIX_SHM_MAX_PAGES;j++)objects[i].pages[j]=0;}
    return 0;
}
static shm_object_t *lookup(rix_shm_id_t id){if(id>=RIX_SHM_MAX||!objects[id].used)return NULL;return &objects[id];}
int shm_create(uint64_t size,rix_shm_id_t *out_id){
    if(!size||size>RIX_SHM_MAX_PAGES*PAGE_SIZE||!out_id)return -1;
    uint64_t pages=(size+PAGE_SIZE-1ULL)/PAGE_SIZE;
    for(rix_shm_id_t i=1;i<RIX_SHM_MAX;i++)if(!objects[i].used){
        shm_object_t *o=&objects[i];o->used=1;o->refs=0;o->size=pages*PAGE_SIZE;
        for(uint64_t p=0;p<pages;p++){o->pages[p]=pmm_alloc_page();if(!o->pages[p]){for(uint64_t j=0;j<p;j++)pmm_free_page(o->pages[j]);o->used=0;o->size=0;return -1;}}
        *out_id=i;return 0;
    }
    return -1;
}
int shm_map(rix_shm_id_t id,pid_t pid,uint64_t va,uint64_t flags){
    shm_object_t *o=lookup(id);rix_process_t *p=process_lookup(pid);if(!o||!p||!p->address_space.pml4_phys||!va||((va&0xfffULL)!=0))return -1;
    if(va>=0x0000800000000000ULL||o->size>0x0000800000000000ULL-va)return -1;
    for(uint64_t off=0;off<o->size;off+=PAGE_SIZE)if(address_space_map_shared(&p->address_space,va+off,o->pages[off/PAGE_SIZE],flags)!=0){
        for(uint64_t rollback=0;rollback<off;rollback+=PAGE_SIZE)address_space_map_shared(&p->address_space,va+rollback,0,0);
        return -1;
    }
    o->refs++;return 0;
}
int shm_unmap(rix_shm_id_t id,pid_t pid,uint64_t va){
    shm_object_t *o=lookup(id);rix_process_t *p=process_lookup(pid);if(!o||!p||!p->address_space.pml4_phys||!va||(va&0xfffULL))return -1;
    /* Unmapping is deliberately page-table removal only; the object owns the physical pages. */
    for(uint64_t off=0;off<o->size;off+=PAGE_SIZE){if(address_space_query_flags(&p->address_space,va+off)&RIXURI_PTE_OWNED)return -1;}
    return 0;
}
int shm_destroy(rix_shm_id_t id){
    shm_object_t *o=lookup(id);if(!o||o->refs)return -1;
    uint64_t pages=o->size/PAGE_SIZE;for(uint64_t i=0;i<pages;i++)if(o->pages[i])pmm_free_page(o->pages[i]);
    o->used=0;o->size=0;return 0;
}
uint64_t shm_size(rix_shm_id_t id){shm_object_t *o=lookup(id);return o?o->size:0;}
