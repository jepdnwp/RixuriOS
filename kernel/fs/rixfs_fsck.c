#include "rixfs.h"
#include "rixfs_dir.h"
#include "../mm/pmm.h"
#include <stddef.h>
#include <stdint.h>

static int bio_read(rix_block_device_t *d,uint64_t sector,void *buffer){
    rix_bio_t b={0}; b.op=RIX_BIO_READ; b.sector=sector; b.count=1; b.buffer=buffer; b.buffer_size=d->sector_size;
    return block_submit(d,&b);
}
static int bitmap_test(rixfs_t *fs,uint64_t sector,int *used){
    uint64_t bits=(uint64_t)fs->device->sector_size*8ULL;
    uint64_t rel=sector/8ULL, bsec=fs->super.bitmap_sector+rel/(uint64_t)fs->device->sector_size;
    uint64_t off=rel%(uint64_t)fs->device->sector_size;
    if(sector>=fs->super.total_sectors||bsec>=fs->super.bitmap_sector+fs->super.bitmap_sectors)return -1;
    uint64_t p=pmm_alloc_page(); if(!p)return -2;
    uint8_t *buf=(uint8_t *)(uintptr_t)p; int r=bio_read(fs->device,bsec,buf);
    if(!r)*used=(buf[off]&(uint8_t)(1u<<(sector%8ULL)))!=0;
    (void)bits; pmm_free_page(p); return r;
}
static int check_inode(rixfs_t *fs,uint64_t ino,uint64_t *referenced){
    rixfs_inode_disk_t in; if(rixfs_read_inode(fs,ino,&in))return -1;
    if(in.inode==0)return 0;
    uint32_t type=in.mode&RIXFS_IFMT;
    if(type!=RIXFS_IFREG&&type!=RIXFS_IFDIR&&type!=RIXFS_IFLNK)return -2;
    uint64_t total=0;
    for(unsigned e=0;e<RIXFS_DIRECT_EXTENTS;e++){
        uint64_t start=in.extent_start[e], len=in.extent_length[e];
        if(!len)continue;
        if(start<fs->super.data_start_sector||start>=fs->super.total_sectors||len>fs->super.total_sectors-start)return -3;
        if(UINT64_MAX-total<len)return -4; total+=len;
        for(uint64_t j=0;j<len;j++){
            int used=0; if(bitmap_test(fs,start+j,&used)||!used)return -5;
            if(referenced) (*referenced)++;
        }
    }
    uint64_t needed=(in.size+(fs->device->sector_size-1ULL))/(uint64_t)fs->device->sector_size;
    if(type==RIXFS_IFREG&&needed>total)return -6;
    if(type==RIXFS_IFDIR){uint64_t off=0;rixfs_dirent_disk_t ent;char name[RIXFS_NAME_MAX+1];while(1){int r=rixfs_readdir(fs,ino,&off,&ent,name,sizeof(name));if(r==1)break;if(r)return -7;if(ent.inode==0||ent.inode>fs->super.inode_count)return -8;}}
    return 0;
}

int rixfs_fsck(rix_block_device_t *device,uint64_t *checked_inodes,uint64_t *referenced_sectors){
    if(checked_inodes)*checked_inodes=0; if(referenced_sectors)*referenced_sectors=0;
    if(!device)return -1;
    rixfs_t fs={0}; int r=rixfs_mount(device,&fs); if(r)return -2;
    if(fs.super.root_inode==0||fs.super.root_inode>fs.super.inode_count){rixfs_unmount(&fs);return -3;}
    uint64_t checked=0,refs=0;
    for(uint64_t ino=1;ino<=fs.super.inode_count;ino++){
        rixfs_inode_disk_t in; if(rixfs_read_inode(&fs,ino,&in)){rixfs_unmount(&fs);return -4;}
        if(in.inode==0)continue;
        if(in.inode!=ino){rixfs_unmount(&fs);return -5;}
        r=check_inode(&fs,ino,&refs); if(r){rixfs_unmount(&fs);return -10+r;}
        checked++;
    }
    rixfs_inode_disk_t root; if(rixfs_read_inode(&fs,fs.super.root_inode,&root)||root.inode!=fs.super.root_inode||(root.mode&RIXFS_IFMT)!=RIXFS_IFDIR){rixfs_unmount(&fs);return -20;}
    rixfs_unmount(&fs); if(checked_inodes)*checked_inodes=checked; if(referenced_sectors)*referenced_sectors=refs; return 0;
}
