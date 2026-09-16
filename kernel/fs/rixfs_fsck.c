#include "rixfs.h"
#include "rixfs_dir.h"
#include "../mm/pmm.h"
#include <stddef.h>
#include <stdint.h>

#define RIXFS_FSCK_MAX_INODES 65536u
static uint8_t fsck_reachable[(RIXFS_FSCK_MAX_INODES+7u)/8u];
static int fsck_repair_ignore_bitmap;

static int bio_read(rix_block_device_t *d,uint64_t sector,void *buffer){
    rix_bio_t b={0}; b.op=RIX_BIO_READ; b.sector=sector; b.count=1; b.buffer=buffer; b.buffer_size=d->sector_size;
    return block_submit(d,&b);
}
static int bitmap_test(rixfs_t *fs,uint64_t sector,int *used){
    if (fsck_repair_ignore_bitmap) { if (used) *used=1; return 0; }
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
static int count_allocated_data_sectors(rixfs_t *fs,uint64_t *count){
    if(!fs||!count||fs->super.data_start_sector>=fs->super.total_sectors)return -1;
    uint64_t total=0;
    for(uint64_t sector=fs->super.data_start_sector;sector<fs->super.total_sectors;sector++){
        int used=0;
        if(bitmap_test(fs,sector,&used)!=0)return -2;
        if(used){if(total==UINT64_MAX)return -3;total++;}
    }
    *count=total;
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
    uint64_t allocated=0;
    if(count_allocated_data_sectors(&fs,&allocated)!=0||allocated!=refs){rixfs_unmount(&fs);return -26;}
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

static int repair_validate_structure(rixfs_t *fs) {
    uint64_t refs=0;
    if (!fs || fs->super.root_inode==0 || fs->super.root_inode>fs->super.inode_count) return -1;
    fsck_repair_ignore_bitmap=1;
    for (uint64_t ino=1;ino<=fs->super.inode_count;ino++) {
        rixfs_inode_disk_t in;
        if (rixfs_read_inode(fs,ino,&in)) { fsck_repair_ignore_bitmap=0; return -2; }
        if (!in.inode) continue;
        if (in.inode!=ino || check_inode(fs,ino,&refs)!=0) { fsck_repair_ignore_bitmap=0; return -3; }
    }
    rixfs_inode_disk_t root;
    if (rixfs_read_inode(fs,fs->super.root_inode,&root) || root.inode!=fs->super.root_inode ||
        (root.mode&RIXFS_IFMT)!=RIXFS_IFDIR || check_reachable(fs)!=0) {
        fsck_repair_ignore_bitmap=0; return -4;
    }
    for (uint64_t ino=1;ino<=fs->super.inode_count;ino++) {
        rixfs_inode_disk_t in; uint32_t links=0;
        if (rixfs_read_inode(fs,ino,&in)) { fsck_repair_ignore_bitmap=0; return -5; }
        if (!in.inode || ino==fs->super.root_inode) continue;
        if (inode_link_count(fs,ino,&links)!=0 || !links ||
            ((in.mode&RIXFS_IFMT)!=RIXFS_IFDIR && in.links!=links)) {
            fsck_repair_ignore_bitmap=0; return -6;
        }
    }
    fsck_repair_ignore_bitmap=0;
    return 0;
}

int rixfs_fsck_repair(rix_block_device_t *device,uint64_t *repaired_sectors) {
    if (repaired_sectors) *repaired_sectors=0;
    if (!device || !device->sector_size || device->sector_size>RIXFS_SECTOR_MAX) return -1;
    rixfs_t fs={0};
    if (rixfs_mount(device,&fs)!=0) return -2;
    if (repair_validate_structure(&fs)!=0) { rixfs_unmount(&fs); return -3; }
    uint64_t changed=0;
    uint64_t bitmap_bytes=fs.super.bitmap_sectors*(uint64_t)device->sector_size;
    if (fs.super.bitmap_sectors==0 || bitmap_bytes<fs.super.total_sectors/8u) { rixfs_unmount(&fs); return -4; }
    uint64_t cur_page=pmm_alloc_page(), want_page=pmm_alloc_page();
    if (!cur_page||!want_page) { if(cur_page)pmm_free_page(cur_page); if(want_page)pmm_free_page(want_page); rixfs_unmount(&fs); return -5; }
    uint8_t *cur=(uint8_t *)(uintptr_t)cur_page, *want=(uint8_t *)(uintptr_t)want_page;
    for (uint64_t bs=0;bs<fs.super.bitmap_sectors;bs++) {
        if (bio_read(device,fs.super.bitmap_sector+bs,cur)!=0) { pmm_free_page(cur_page);pmm_free_page(want_page);rixfs_unmount(&fs);return -6; }
        for (uint32_t i=0;i<device->sector_size;i++) want[i]=0;
        uint64_t first=bs*(uint64_t)device->sector_size*8u;
        uint64_t last=first+(uint64_t)device->sector_size*8u;
        if (last>fs.super.total_sectors) last=fs.super.total_sectors;
        for (uint64_t s=first;s<last;s++) if (s<fs.super.data_start_sector) want[(s-first)>>3]|=(uint8_t)(1u<<(s&7u));
        for (uint64_t ino=1;ino<=fs.super.inode_count;ino++) {
            rixfs_inode_disk_t in;
            if (rixfs_read_inode(&fs,ino,&in)) { pmm_free_page(cur_page);pmm_free_page(want_page);rixfs_unmount(&fs);return -7; }
            if (!in.inode) continue;
            for (unsigned e=0;e<RIXFS_DIRECT_EXTENTS;e++) for (uint64_t j=0;j<in.extent_length[e];j++) {
                uint64_t s=in.extent_start[e]+j;
                if (s>=first&&s<last) want[(s-first)>>3]|=(uint8_t)(1u<<(s&7u));
            }
        }
        for (uint32_t i=0;i<device->sector_size;i++) {
            uint8_t delta=(uint8_t)(cur[i]^want[i]);
            for (unsigned bit=0;bit<8;bit++) if (delta&(uint8_t)(1u<<bit)) changed++;
        }
        int different=0; for (uint32_t i=0;i<device->sector_size;i++) if(cur[i]!=want[i]) {different=1;break;}
        if (different && block_submit(device,&(rix_bio_t){RIX_BIO_WRITE,fs.super.bitmap_sector+bs,1,want,device->sector_size,RIX_BIO_PENDING,0})!=0) { pmm_free_page(cur_page);pmm_free_page(want_page);rixfs_unmount(&fs);return -8; }
    }
    rix_bio_t flush={RIX_BIO_FLUSH,0,0,NULL,0,RIX_BIO_PENDING,0};
    int rc=block_submit(device,&flush);
    pmm_free_page(cur_page);pmm_free_page(want_page);rixfs_unmount(&fs);
    if (rc!=0) return -9;
    if (repaired_sectors) *repaired_sectors=changed;
    return 0;
}
