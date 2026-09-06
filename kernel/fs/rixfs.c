#include "rixfs.h"
#include "../mm/pmm.h"
#include <stddef.h>
#include <stdint.h>

#define RIXFS_PAGE_SIZE 4096u
#define RIXFS_MIN_HEADER 64u

static uint64_t checksum(const void *data,size_t size){
    const uint8_t *p=(const uint8_t *)data;
    uint64_t h=1469598103934665603ULL;
    for(size_t i=0;i<size;i++)h=(h^(uint64_t)p[i])*1099511628211ULL;
    return h;
}
static int bio_read(rix_block_device_t *d,uint64_t sector,uint32_t count,void *buf,size_t bytes){
    rix_bio_t bio={0};bio.op=RIX_BIO_READ;bio.sector=sector;bio.count=count;bio.buffer=buf;bio.buffer_size=bytes;
    return block_submit(d,&bio);
}
static int bio_write(rix_block_device_t *d,uint64_t sector,uint32_t count,const void *buf,size_t bytes){
    rix_bio_t bio={0};bio.op=RIX_BIO_WRITE;bio.sector=sector;bio.count=count;bio.buffer=(void *)buf;bio.buffer_size=bytes;
    return block_submit(d,&bio);
}
static int inode_location(const rixfs_t *fs,uint64_t inode,uint64_t *sector,uint32_t *offset){
    if(!fs||!fs->mounted||!sector||!offset||inode==0||inode>fs->super.inode_count)return -1;
    uint64_t byte_index=(inode-1ULL)*(uint64_t)RIXFS_INODE_SIZE;
    uint64_t sectors=byte_index/(uint64_t)fs->device->sector_size;
    uint64_t in_sector=byte_index%(uint64_t)fs->device->sector_size;
    if(in_sector+RIXFS_INODE_SIZE>fs->device->sector_size)return -1;
    if(fs->super.inode_table_sector>fs->super.total_sectors||sectors>fs->super.total_sectors-fs->super.inode_table_sector)return -1;
    *sector=fs->super.inode_table_sector+sectors;*offset=(uint32_t)in_sector;return 0;
}
static int extent_map(const rixfs_inode_disk_t *in,uint64_t logical_sector,uint64_t *physical_sector){
    uint64_t pos=logical_sector;
    for(unsigned i=0;i<RIXFS_DIRECT_EXTENTS;i++){
        uint64_t len=in->extent_length[i];
        if(!len)continue;
        if(pos<len){*physical_sector=in->extent_start[i]+pos;return 0;}
        pos-=len;
    }
    return -1;
}

int rixfs_mount(rix_block_device_t *device,rixfs_t *fs){
    if(!device||!fs||device->sector_size==0||device->sector_size>RIXFS_PAGE_SIZE||device->sector_count==0)return -1;
    uint64_t page=pmm_alloc_page();if(!page)return -2;
    uint8_t *buf=(uint8_t *)(uintptr_t)page;
    for(size_t i=0;i<RIXFS_PAGE_SIZE;i++)buf[i]=0;
    if(bio_read(device,RIXFS_BLOCK_SECTOR,1,buf,device->sector_size)!=0){pmm_free_page(page);return -3;}
    if(device->sector_size<sizeof(rixfs_superblock_t)){pmm_free_page(page);return -4;}
    rixfs_superblock_t sb;for(size_t i=0;i<sizeof(sb);i++)((uint8_t *)&sb)[i]=buf[i];
    uint64_t stored=sb.checksum;sb.checksum=0;
    if(sb.magic!=RIXFS_MAGIC||sb.version!=RIXFS_VERSION||sb.header_size< RIXFS_MIN_HEADER||sb.header_size>sizeof(rixfs_superblock_t)||stored!=checksum(&sb,sizeof(sb))){pmm_free_page(page);return -5;}
    sb.checksum=stored;
    if(sb.sector_size!=device->sector_size||sb.total_sectors!=device->sector_count||sb.inode_table_sector==0||sb.inode_count==0||sb.root_inode==0||sb.root_inode>sb.inode_count||sb.data_start_sector>=sb.total_sectors){pmm_free_page(page);return -6;}
    uint64_t inode_bytes=sb.inode_count*(uint64_t)RIXFS_INODE_SIZE;if(sb.inode_count&&inode_bytes/(uint64_t)RIXFS_INODE_SIZE!=sb.inode_count){pmm_free_page(page);return -7;}
    uint64_t inode_sectors=(inode_bytes+(device->sector_size-1u))/(uint64_t)device->sector_size;
    if(sb.inode_table_sector>sb.data_start_sector||inode_sectors>sb.data_start_sector-sb.inode_table_sector){pmm_free_page(page);return -8;}
    fs->device=device;fs->super=sb;fs->mounted=1;pmm_free_page(page);return 0;
}

int rixfs_read_inode(rixfs_t *fs,uint64_t inode,rixfs_inode_disk_t *out){
    if(!fs||!out||!fs->mounted)return -1;uint64_t sector;uint32_t off;if(inode_location(fs,inode,&sector,&off)!=0)return -2;
    uint64_t page=pmm_alloc_page();if(!page)return -3;uint8_t *buf=(uint8_t *)(uintptr_t)page;
    int rc=bio_read(fs->device,sector,1,buf,fs->device->sector_size);if(rc==0)for(size_t i=0;i<sizeof(*out);i++)((uint8_t *)out)[i]=buf[off+i];pmm_free_page(page);return rc;
}

static int io_file(rixfs_t *fs,uint64_t inode,uint64_t offset,void *buffer,size_t size,int write){
    if(!fs||!fs->mounted||(!buffer&&size))return -1;if(!size)return 0;if(offset>UINT64_MAX-(uint64_t)size)return -2;
    rixfs_inode_disk_t in;if(rixfs_read_inode(fs,inode,&in)!=0)return -3;if(in.inode!=inode||offset>(uint64_t)in.size||size>(size_t)((uint64_t)in.size-offset))return -4;
    uint64_t sector_size=fs->device->sector_size;uint64_t first=offset/sector_size,last=(offset+(uint64_t)size-1ULL)/sector_size;
    uint64_t page=pmm_alloc_page();if(!page)return -5;uint8_t *tmp=(uint8_t *)(uintptr_t)page;size_t done=0;
    for(uint64_t ls=first;ls<=last;ls++){
        uint64_t ps;if(extent_map(&in,ls,&ps)!=0||ps>=fs->super.total_sectors){pmm_free_page(page);return -6;}
        if(bio_read(fs->device,ps,1,tmp,fs->device->sector_size)!=0){pmm_free_page(page);return -7;}
        uint64_t begin=(ls==first)?offset%sector_size:0;uint64_t end=(ls==last)?((offset+(uint64_t)size-1ULL)%sector_size)+1:sector_size;size_t n=(size_t)(end-begin);
        if(write){for(size_t i=0;i<n;i++)tmp[begin+i]=((const uint8_t *)buffer)[done+i];if(bio_write(fs->device,ps,1,tmp,fs->device->sector_size)!=0){pmm_free_page(page);return -8;}}
        else for(size_t i=0;i<n;i++)((uint8_t *)buffer)[done+i]=tmp[begin+i];
        done+=n;
    }
    pmm_free_page(page);return 0;
}
int rixfs_read(rixfs_t *fs,uint64_t inode,uint64_t offset,void *buffer,size_t size){return io_file(fs,inode,offset,buffer,size,0);}
int rixfs_write(rixfs_t *fs,uint64_t inode,uint64_t offset,const void *buffer,size_t size){return io_file(fs,inode,offset,(void *)buffer,size,1);}
int rixfs_sync(rixfs_t *fs){if(!fs||!fs->mounted)return -1;rix_bio_t bio={0};bio.op=RIX_BIO_FLUSH;return block_submit(fs->device,&bio);}
void rixfs_unmount(rixfs_t *fs){if(!fs)return;fs->mounted=0;fs->device=NULL;for(size_t i=0;i<sizeof(fs->super);i++)((uint8_t *)&fs->super)[i]=0;}
