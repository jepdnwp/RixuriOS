#include "rixfs.h"
#include "rixfs_dir.h"
#include "../mm/pmm.h"
#include <stddef.h>
#include <stdint.h>

#define RIXFS_FSCK_MAX_INODES 65536u
static uint8_t fsck_reachable[(RIXFS_FSCK_MAX_INODES+7u)/8u];

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
static int extent_overlap(uint64_t a_start,uint64_t a_len,uint64_t b_start,uint64_t b_len){
    if(!a_len||!b_len)return 0;
    return (a_start<b_start?(b_start-a_start)<a_len:(a_start-b_start)<b_len);
}
static int check_inode(rixfs_t *fs,uint64_t ino,uint64_t *referenced){
    rixfs_inode_disk_t in; if(rixfs_read_inode(fs,ino,&in))return -1;
    if(in.inode==0)return 0;
    uint32_t type=in.mode&RIXFS_IFMT;
    if(type!=RIXFS_IFREG&&type!=RIXFS_IFDIR&&type!=RIXFS_IFLNK)return -2;
    for(unsigned a=0;a<RIXFS_DIRECT_EXTENTS;a++)
        for(unsigned b=a+1;b<RIXFS_DIRECT_EXTENTS;b++)
            if(extent_overlap(in.extent_start[a],in.extent_length[a],
                              in.extent_start[b],in.extent_length[b]))return -9;
    /* Each data sector must have one inode owner. Shared extents make
     * truncate/unlink and recovery order-dependent, so reject them before
     * reporting the image as clean. */
    for(uint64_t prior=1;prior<ino;prior++){
        rixfs_inode_disk_t other;
        if(rixfs_read_inode(fs,prior,&other))return -10;
        if(!other.inode)continue;
        for(unsigned a=0;a<RIXFS_DIRECT_EXTENTS;a++)
            for(unsigned b=0;b<RIXFS_DIRECT_EXTENTS;b++)
                if(extent_overlap(in.extent_start[a],in.extent_length[a],
                                  other.extent_start[b],other.extent_length[b]))return -9;
    }
    uint64_t total=0;
    for(unsigned e=0;e<RIXFS_DIRECT_EXTENTS;e++){
        uint64_t start=in.extent_start[e], len=in.extent_length[e];
        if(!len)continue;
        if(start<fs->super.data_start_sector||start>=fs->super.total_sectors||len>fs->super.total_sectors-start)return -3;
        if(UINT64_MAX-total<len)return -4;
        total+=len;
        for(uint64_t j=0;j<len;j++){
            int used=0; if(bitmap_test(fs,start+j,&used)||!used)return -5;
            if(referenced) (*referenced)++;
        }
    }
    uint64_t needed=(in.size+(fs->device->sector_size-1ULL))/(uint64_t)fs->device->sector_size;
    if(type==RIXFS_IFREG&&needed>total)return -6;
    if(type==RIXFS_IFDIR){uint64_t off=0;rixfs_dirent_disk_t ent;char name[RIXFS_NAME_MAX+1];while(1){int r=rixfs_readdir(fs,ino,&off,&ent,name,sizeof(name));if(r==1)break;if(r)return -7;if(ent.inode==0||ent.inode>fs->super.inode_count)return -8;rixfs_inode_disk_t child;if(rixfs_read_inode(fs,ent.inode,&child)||!child.inode)return -8;uint32_t child_type=child.mode&RIXFS_IFMT;uint8_t expected=child_type==RIXFS_IFDIR?RIXFS_DIR_TYPE_DIR:(child_type==RIXFS_IFLNK?RIXFS_DIR_TYPE_SYMLINK:RIXFS_DIR_TYPE_FILE);if(ent.type!=expected)return -8;}}
    return 0;
}

static int inode_link_count(rixfs_t *fs,uint64_t target,uint32_t *links){
    *links=0;
    for(uint64_t dir=1;dir<=fs->super.inode_count;dir++){
        rixfs_inode_disk_t in;
        if(rixfs_read_inode(fs,dir,&in))return -1;
        if(!in.inode||(in.mode&RIXFS_IFMT)!=RIXFS_IFDIR)continue;
        uint64_t off=0; rixfs_dirent_disk_t ent; char name[RIXFS_NAME_MAX+1];
        for(;;){
            int r=rixfs_readdir(fs,dir,&off,&ent,name,sizeof(name));
            if(r==1)break;
            if(r)return -2;
            if(ent.inode==target){if(*links==UINT32_MAX)return -3;(*links)++;}
        }
    }
    return 0;
}
static int check_reachable(rixfs_t *fs){
    uint64_t bytes=(fs->super.inode_count+7u)/8u;
    if(bytes==0||bytes>sizeof(fsck_reachable))return -1;
    uint8_t *seen=fsck_reachable;
    for(size_t i=0;i<sizeof(fsck_reachable);i++)seen[i]=0;
    seen[fs->super.root_inode>>3]|=(uint8_t)(1u<<(fs->super.root_inode&7u));
    int changed=1;
    while(changed){
        changed=0;
        for(uint64_t dir=1;dir<=fs->super.inode_count;dir++){
            if(!(seen[dir>>3]&(uint8_t)(1u<<(dir&7u))))continue;
            rixfs_inode_disk_t in;
            if(rixfs_read_inode(fs,dir,&in)){
                return -2;
            }
            if((in.mode&RIXFS_IFMT)!=RIXFS_IFDIR)continue;
            uint64_t off=0; rixfs_dirent_disk_t ent; char name[RIXFS_NAME_MAX+1];
            for(;;){
                int r=rixfs_readdir(fs,dir,&off,&ent,name,sizeof(name));
                if(r==1)break;
                if(r||ent.inode==0||ent.inode>fs->super.inode_count){
                    return -3;
                }
                uint8_t *slot=&seen[ent.inode>>3];
                uint8_t bit=(uint8_t)(1u<<(ent.inode&7u));
                if(!(*slot&bit)){*slot|=bit;changed=1;}
            }
        }
    }
    for(uint64_t ino=1;ino<=fs->super.inode_count;ino++){
        rixfs_inode_disk_t in;
        if(rixfs_read_inode(fs,ino,&in))return -4;
        if(in.inode&&!((seen[ino>>3]>>(ino&7u))&1u)){
            return -5;
        }
    }
    return 0;
}

int rixfs_fsck(rix_block_device_t *device,uint64_t *checked_inodes,uint64_t *referenced_sectors){
    if(checked_inodes)*checked_inodes=0;
    if(referenced_sectors)*referenced_sectors=0;
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
    if(check_reachable(&fs)!=0){rixfs_unmount(&fs);return -25;}
    for(uint64_t ino=1;ino<=fs.super.inode_count;ino++){
        rixfs_inode_disk_t in; uint32_t links=0;
        if(rixfs_read_inode(&fs,ino,&in)){rixfs_unmount(&fs);return -21;}
        if(!in.inode||ino==fs.super.root_inode)continue;
        if(inode_link_count(&fs,ino,&links)!=0){rixfs_unmount(&fs);return -22;}
        if(!links){rixfs_unmount(&fs);return -23;}
        if((in.mode&RIXFS_IFMT)!=RIXFS_IFDIR&&in.links!=links){rixfs_unmount(&fs);return -24;}
    }
    rixfs_unmount(&fs); if(checked_inodes)*checked_inodes=checked; if(referenced_sectors)*referenced_sectors=refs; return 0;
}
