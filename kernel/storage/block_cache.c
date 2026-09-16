#include "block_cache.h"
#include "../sync/lock.h"
#include <stddef.h>
#include <stdint.h>

#define CACHE_ENTRIES 64
#define CACHE_SECTOR_BYTES 4096

typedef struct { rix_block_device_t *device; uint64_t sector; uint8_t valid,dirty,writeback; uint8_t data[CACHE_SECTOR_BYTES]; uint64_t age; } cache_entry_t;
static cache_entry_t cache[CACHE_ENTRIES];
static rix_spinlock_t cache_lock;
static uint64_t clock_tick;

int block_cache_init(void){for(size_t i=0;i<CACHE_ENTRIES;i++){cache[i].device=NULL;cache[i].valid=cache[i].dirty=cache[i].writeback=0;cache[i].age=0;}clock_tick=0;rix_spin_init(&cache_lock);return 0;}
static int io(rix_block_device_t*d,rix_bio_op_t op,uint64_t sector,void*b){if(!d||!d->submit||d->sector_size==0||d->sector_size>CACHE_SECTOR_BYTES)return -1;rix_bio_t bio={op,sector,1,b,d->sector_size,RIX_BIO_PENDING,0};int r=d->submit(d,&bio);return r||bio.state==RIX_BIO_ERROR||bio.state==RIX_BIO_TIMEOUT?-1:0;}
static cache_entry_t *find(rix_block_device_t*d,uint64_t s){for(size_t i=0;i<CACHE_ENTRIES;i++)if(cache[i].valid&&cache[i].device==d&&cache[i].sector==s)return &cache[i];return NULL;}
static cache_entry_t *victim(void){cache_entry_t*v=NULL;for(size_t i=0;i<CACHE_ENTRIES;i++){if(cache[i].writeback)continue;if(!cache[i].valid)return &cache[i];if(!v||cache[i].age<v->age)v=&cache[i];}return v;}
static void invalidate(cache_entry_t*e){e->valid=0;e->dirty=0;e->writeback=0;e->device=NULL;e->sector=0;e->age=0;}
/* Evict one victim slot, preserving dirty data on writeback failure.
 * Returns 0 with a free (invalid) slot ready in *out_slot, or -1 when the
 * chosen dirty victim could not be written back (its entry is left
 * valid+dirty, never discarded). Uses the victim device's own sector size,
 * never the incoming device's size. Bounded: single victim, no recursion. */
static int evict_one(cache_entry_t **out_slot){
    uint64_t f=0;rix_spin_lock_irqsave(&cache_lock,&f);
    cache_entry_t*e=victim();
    if(!e){rix_spin_unlock_irqrestore(&cache_lock,f);return -1;}
    if(!e->valid||!e->dirty){
        invalidate(e);
        rix_spin_unlock_irqrestore(&cache_lock,f);
        *out_slot=e;
        return 0;
    }
    rix_block_device_t*old_d=e->device;uint64_t old_s=e->sector;uint64_t old_age=e->age;
    uint32_t old_sz=old_d?old_d->sector_size:0;
    if(!old_d||old_sz==0||old_sz>CACHE_SECTOR_BYTES){rix_spin_unlock_irqrestore(&cache_lock,f);return -1;}
    uint8_t old_data[CACHE_SECTOR_BYTES];
    for(uint32_t i=0;i<old_sz;i++)old_data[i]=e->data[i];
    e->writeback=1;
    rix_spin_unlock_irqrestore(&cache_lock,f);
    if(io(old_d,RIX_BIO_WRITE,old_s,old_data)!=0){
        rix_spin_lock_irqsave(&cache_lock,&f);
        cache_entry_t*failed=find(old_d,old_s);
        if(failed&&failed->age==old_age)failed->writeback=0;
        rix_spin_unlock_irqrestore(&cache_lock,f);
        return -1;
    }
    rix_spin_lock_irqsave(&cache_lock,&f);
    /* Only reclaim when nobody touched the entry while we wrote. A
     * concurrent update bumps age, so the new dirty data must survive. */
    cache_entry_t*cur=find(old_d,old_s);
    if(cur&&cur->age==old_age)invalidate(cur);
    else if(cur)cur->writeback=0;
    /* The invalidated slot (or another free one) is now available. */
    e=victim();
    if(!e||e->valid){
        /* Concurrent fill raced us; leave state intact and let the caller
         * retry its outer loop (bounded by the caller). */
        rix_spin_unlock_irqrestore(&cache_lock,f);
        return -1;
    }
    rix_spin_unlock_irqrestore(&cache_lock,f);
    *out_slot=e;
    return 0;
}
int block_cache_read(rix_block_device_t*d,uint64_t s,void*b){
 if(!d||!b||d->sector_size==0||d->sector_size>CACHE_SECTOR_BYTES)return -1;
 uint64_t f=0;rix_spin_lock_irqsave(&cache_lock,&f);cache_entry_t*e=find(d,s);if(e){e->age=++clock_tick;for(uint32_t i=0;i<d->sector_size;i++)((uint8_t*)b)[i]=e->data[i];rix_spin_unlock_irqrestore(&cache_lock,f);return 0;}rix_spin_unlock_irqrestore(&cache_lock,f);
 cache_entry_t*slot=NULL;
 for(int i=0;i<3;i++){if(evict_one(&slot)==0)break;slot=NULL;if(i==2)return -1;}
 uint8_t fresh[CACHE_SECTOR_BYTES];if(io(d,RIX_BIO_READ,s,fresh)!=0)return -1;
 rix_spin_lock_irqsave(&cache_lock,&f);if(find(d,s)){rix_spin_unlock_irqrestore(&cache_lock,f);for(uint32_t i=0;i<d->sector_size;i++)((uint8_t*)b)[i]=fresh[i];return 0;}e=victim();if(!e||e->valid){rix_spin_unlock_irqrestore(&cache_lock,f);for(uint32_t i=0;i<d->sector_size;i++)((uint8_t*)b)[i]=fresh[i];return 0;}for(uint32_t i=0;i<d->sector_size;i++)e->data[i]=fresh[i];e->device=d;e->sector=s;e->valid=1;e->dirty=0;e->writeback=0;e->age=++clock_tick;for(uint32_t i=0;i<d->sector_size;i++)((uint8_t*)b)[i]=e->data[i];rix_spin_unlock_irqrestore(&cache_lock,f);return 0;
}
int block_cache_write(rix_block_device_t*d,uint64_t s,const void*b){
 if(!d||!b||d->sector_size==0||d->sector_size>CACHE_SECTOR_BYTES)return -1;
 uint8_t input[CACHE_SECTOR_BYTES];for(uint32_t i=0;i<d->sector_size;i++)input[i]=((const uint8_t*)b)[i];uint64_t f=0;rix_spin_lock_irqsave(&cache_lock,&f);cache_entry_t*e=find(d,s);if(e){for(uint32_t i=0;i<d->sector_size;i++)e->data[i]=input[i];e->dirty=1;e->age=++clock_tick;rix_spin_unlock_irqrestore(&cache_lock,f);return 0;}rix_spin_unlock_irqrestore(&cache_lock,f);
 cache_entry_t*slot=NULL;
 for(int i=0;i<3;i++){if(evict_one(&slot)==0)break;slot=NULL;if(i==2)return -1;}
 rix_spin_lock_irqsave(&cache_lock,&f);e=victim();if(!e){rix_spin_unlock_irqrestore(&cache_lock,f);return io(d,RIX_BIO_WRITE,s,input);}if(e->valid){rix_spin_unlock_irqrestore(&cache_lock,f);return io(d,RIX_BIO_WRITE,s,input);}for(uint32_t i=0;i<d->sector_size;i++)e->data[i]=input[i];e->device=d;e->sector=s;e->valid=1;e->dirty=1;e->writeback=0;e->age=++clock_tick;rix_spin_unlock_irqrestore(&cache_lock,f);return 0;
}
 int block_cache_flush(rix_block_device_t*d){if(!d||!d->submit)return -1;for(;;){uint64_t f=0;rix_spin_lock_irqsave(&cache_lock,&f);cache_entry_t*e=NULL;for(size_t i=0;i<CACHE_ENTRIES;i++)if(cache[i].valid&&cache[i].device==d&&cache[i].dirty&&!cache[i].writeback){e=&cache[i];break;}if(!e){rix_spin_unlock_irqrestore(&cache_lock,f);break;}uint64_t sector=e->sector;uint64_t age=e->age;uint8_t data[CACHE_SECTOR_BYTES];for(uint32_t i=0;i<d->sector_size;i++)data[i]=e->data[i];e->writeback=1;rix_spin_unlock_irqrestore(&cache_lock,f);int wr=io(d,RIX_BIO_WRITE,sector,data);rix_spin_lock_irqsave(&cache_lock,&f);cache_entry_t*x=find(d,sector);if(x){x->writeback=0;if(wr==0&&x->age==age)x->dirty=0;}rix_spin_unlock_irqrestore(&cache_lock,f);if(wr!=0)return -1;}
 rix_bio_t bio={RIX_BIO_FLUSH,0,0,NULL,0,RIX_BIO_PENDING,0};return d->submit(d,&bio);}
