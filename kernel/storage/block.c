#include "block.h"
#include <stddef.h>

static rix_block_device_t *devices[RIX_BLOCK_MAX_DEVICES];
static size_t device_count;

static size_t name_len(const char *s){size_t n=0;if(!s)return 0;while(s[n]&&n<RIX_BLOCK_NAME_MAX)n++;return n;}
static int name_eq(const char *a,const char *b){size_t i=0;if(!a||!b)return 0;while(a[i]&&b[i]&&i<RIX_BLOCK_NAME_MAX){if(a[i]!=b[i])return 0;i++;}return a[i]==b[i];}

int block_init(void){for(size_t i=0;i<RIX_BLOCK_MAX_DEVICES;i++)devices[i]=NULL;device_count=0;return 0;}
int block_register(rix_block_device_t *device){
    if(!device||!device->name[0]||!device->sector_size||!device->sector_count||!device->submit)return -1;
    if(name_len(device->name)>=RIX_BLOCK_NAME_MAX)return -1;
    if(block_find(device->name))return -1;
    if(device->max_sectors==0||device->max_sectors>RIX_BIO_MAX_SECTORS)device->max_sectors=RIX_BIO_MAX_SECTORS;
    if(device_count>=RIX_BLOCK_MAX_DEVICES)return -1;devices[device_count++]=device;return 0;
}
rix_block_device_t *block_find(const char *name){for(size_t i=0;i<device_count;i++)if(name_eq(devices[i]->name,name))return devices[i];return NULL;}
int block_submit(rix_block_device_t *device,rix_bio_t *bio){
    if(!device||!bio||!device->submit||bio->count==0||bio->count>device->max_sectors)return -1;
    if(bio->op!=RIX_BIO_FLUSH){if(!bio->buffer||bio->sector>=device->sector_count||bio->count>device->sector_count-bio->sector)return -1;size_t bytes=(size_t)bio->count*(size_t)device->sector_size;if(bytes/bio->count!=(size_t)device->sector_size||bio->buffer_size<bytes)return -1;}
    bio->state=RIX_BIO_PENDING;bio->error=0;int rc=device->submit(device,bio);if(rc!=0){bio->state=RIX_BIO_ERROR;bio->error=rc;return rc;}if(bio->state==RIX_BIO_PENDING)bio->state=RIX_BIO_COMPLETE;return 0;
}
size_t block_device_count(void){return device_count;}
