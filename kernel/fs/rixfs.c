#include "rixfs.h"
#include "../mm/pmm.h"
#include <stddef.h>
#include <stdint.h>
#define RIXFS_PAGE_SIZE 4096u
#define RIXFS_MIN_HEADER 64u
#define RIXFS_JOURNAL_MAGIC 0x524A4E4C56310001ULL
static uint64_t checksum(const void*data,size_t size){const uint8_t*p=(const uint8_t*)data;uint64_t h=1469598103934665603ULL;for(size_t i=0;i<size;i++)h=(h^(uint64_t)p[i])*1099511628211ULL;return h;}
static int bio_read(rix_block_device_t*d,uint64_t s,uint32_t c,void*b,size_t n){rix_bio_t x={0};x.op=RIX_BIO_READ;x.sector=s;x.count=c;x.buffer=b;x.buffer_size=n;return block_submit(d,&x);}
static int bio_write(rix_block_device_t*d,uint64_t s,uint32_t c,const void*b,size_t n){rix_bio_t x={0};x.op=RIX_BIO_WRITE;x.sector=s;x.count=c;x.buffer=(void*)b;x.buffer_size=n;return block_submit(d,&x);}
static int bio_flush(rix_block_device_t*d){rix_bio_t x={0};x.op=RIX_BIO_FLUSH;return block_submit(d,&x);}
static int inode_location(const rixfs_t*f,uint64_t ino,uint64_t*s,uint32_t*o){if(!f||!f->mounted||!s||!o||!ino||ino>f->super.inode_count)return-1;uint64_t x=(ino-1)*RIXFS_INODE_SIZE,ss=f->device->sector_size;*s=f->super.inode_table_sector+x/ss;*o=(uint32_t)(x%ss);if(*o+RIXFS_INODE_SIZE>ss)return-2;return 0;}
static int extent_map(const rixfs_inode_disk_t*in,uint64_t logical,uint64_t*physical){uint64_t pos=logical;for(unsigned i=0;i<RIXFS_DIRECT_EXTENTS;i++){uint64_t n=in->extent_length[i];if(!n)continue;if(pos<n){if(in->extent_start[i]>UINT64_MAX-pos)return-1;*physical=in->extent_start[i]+pos;return 0;}pos-=n;}return-1;}
static uint64_t load64(const uint8_t *p) {
    uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i) value |= (uint64_t)p[i] << (8u * i);
    return value;
}

static uint32_t load32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) |
           ((uint32_t)p[2] << 16u) | ((uint32_t)p[3] << 24u);
}

static int journal_replay(rixfs_t *f) {
    if (f->super.journal_sectors < 2) return 0;
    uint64_t js = f->super.journal_sector;
    if (js + 1 >= f->super.total_sectors) return -1;
    uint64_t q = pmm_alloc_page();
    if (!q) return -2;
    uint8_t *buffer = (uint8_t *)(uintptr_t)q;
    if (bio_read(f->device, js, 1, buffer, f->device->sector_size)) {
        pmm_free_page(q);
        return -3;
    }
    uint64_t magic = load64(buffer);
    if (magic == RIXFS_JOURNAL_TX_MAGIC) {
        uint32_t count = load32(buffer + 16u);
        uint64_t targets[RIXFS_JOURNAL_TX_MAX_ENTRIES];
        uint64_t checksums[RIXFS_JOURNAL_TX_MAX_ENTRIES];
        if (count == 0 || count > RIXFS_JOURNAL_TX_MAX_ENTRIES ||
            (uint64_t)count + 1u > f->super.journal_sectors ||
            24u + (uint64_t)count * 16u > f->device->sector_size) {
            pmm_free_page(q);
            return -4;
        }
        for (uint32_t i = 0; i < count; ++i) {
            targets[i] = load64(buffer + 24u + (size_t)i * 16u);
            checksums[i] = load64(buffer + 32u + (size_t)i * 16u);
            if (targets[i] >= f->super.total_sectors ||
                (targets[i] >= js && targets[i] < js + f->super.journal_sectors)) {
                pmm_free_page(q);
                return -5;
            }
        }
        for (uint32_t i = 0; i < count; ++i) {
            if (bio_read(f->device, js + 1u + i, 1, buffer,
                         f->device->sector_size) ||
                checksum(buffer, f->device->sector_size) != checksums[i] ||
                bio_write(f->device, targets[i], 1, buffer,
                          f->device->sector_size)) {
                pmm_free_page(q);
                return -6;
            }
        }
        int result = bio_flush(f->device);
        for (uint32_t i = 0; i < f->device->sector_size; ++i) buffer[i] = 0;
        if (!result) result = bio_write(f->device, js, 1, buffer,
                                        f->device->sector_size);
        if (!result) result = bio_flush(f->device);
        pmm_free_page(q);
        return result;
    }
    if (magic != RIXFS_JOURNAL_MAGIC) {
        pmm_free_page(q);
        return 0;
    }
    uint64_t target = load64(buffer + 8u);
    uint64_t sequence = load64(buffer + 16u);
    uint64_t stored = load64(buffer + 32u);
    if (!sequence || target >= f->super.total_sectors ||
        (target >= js && target < js + f->super.journal_sectors)) {
        pmm_free_page(q);
        return -7;
    }
    if (bio_read(f->device, js + 1u, 1, buffer, f->device->sector_size) ||
        checksum(buffer, f->device->sector_size) != stored ||
        bio_write(f->device, target, 1, buffer, f->device->sector_size) ||
        bio_flush(f->device)) {
        pmm_free_page(q);
        return -8;
    }
    for (uint32_t i = 0; i < f->device->sector_size; ++i) buffer[i] = 0;
    int result = bio_write(f->device, js, 1, buffer, f->device->sector_size);
    if (!result) result = bio_flush(f->device);
    pmm_free_page(q);
    return result;
}

int rixfs_mount(rix_block_device_t*device,rixfs_t*fs){if(!device||!fs||device->sector_size<512||device->sector_size>RIXFS_PAGE_SIZE||device->sector_count==0)return-1;uint64_t q=pmm_alloc_page();if(!q)return-2;uint8_t*b=(uint8_t*)(uintptr_t)q;for(size_t i=0;i<RIXFS_PAGE_SIZE;i++)b[i]=0;if(bio_read(device,RIXFS_BLOCK_SECTOR,1,b,device->sector_size)){pmm_free_page(q);return-3;}if(device->sector_size<sizeof(rixfs_superblock_t)){pmm_free_page(q);return-4;}rixfs_superblock_t sb;for(size_t i=0;i<sizeof(sb);i++)((uint8_t*)&sb)[i]=b[i];uint64_t stored=sb.checksum;sb.checksum=0;if(sb.magic!=RIXFS_MAGIC||sb.version!=RIXFS_VERSION||sb.header_size<RIXFS_MIN_HEADER||sb.header_size>sizeof(sb)||stored!=checksum(&sb,sizeof(sb))){pmm_free_page(q);return-5;}sb.checksum=stored;if(sb.sector_size!=device->sector_size||sb.total_sectors!=device->sector_count||!sb.inode_table_sector||!sb.inode_count||!sb.root_inode||sb.root_inode>sb.inode_count||sb.data_start_sector>=sb.total_sectors||!sb.bitmap_sector||!sb.bitmap_sectors||sb.journal_sector<sb.bitmap_sector||sb.journal_sectors<2){pmm_free_page(q);return-6;}uint64_t inode_bytes=sb.inode_count*(uint64_t)RIXFS_INODE_SIZE;if(sb.inode_count&&inode_bytes/(uint64_t)RIXFS_INODE_SIZE!=sb.inode_count){pmm_free_page(q);return-7;}uint64_t inode_sectors=(inode_bytes+(device->sector_size-1))/device->sector_size;if(sb.inode_table_sector>sb.data_start_sector||inode_sectors>sb.data_start_sector-sb.inode_table_sector||sb.bitmap_sector+sb.bitmap_sectors>sb.data_start_sector||sb.journal_sector+sb.journal_sectors>sb.data_start_sector){pmm_free_page(q);return-8;}fs->device=device;fs->super=sb;fs->mounted=1;pmm_free_page(q);if(journal_replay(fs)!=0){rixfs_unmount(fs);return-9;}return 0;}
int rixfs_read_inode(rixfs_t*fs,uint64_t ino,rixfs_inode_disk_t*out){if(!fs||!out||!fs->mounted)return-1;uint64_t s;uint32_t off;if(inode_location(fs,ino,&s,&off))return-2;uint64_t q=pmm_alloc_page();if(!q)return-3;uint8_t*b=(uint8_t*)(uintptr_t)q;int r=bio_read(fs->device,s,1,b,fs->device->sector_size);if(!r)for(size_t i=0;i<sizeof(*out);i++)((uint8_t*)out)[i]=b[off+i];pmm_free_page(q);return r;}
static int io_file(rixfs_t*fs,uint64_t ino,uint64_t off,void*buf,size_t size,int write){if(!fs||!fs->mounted||(!buf&&size))return-1;if(!size)return 0;if(off>UINT64_MAX-(uint64_t)size)return-2;rixfs_inode_disk_t in;if(rixfs_read_inode(fs,ino,&in))return-3;if(in.inode!=ino||off>(uint64_t)in.size||size>(size_t)((uint64_t)in.size-off))return-4;uint64_t ss=fs->device->sector_size,first=off/ss,last=(off+(uint64_t)size-1)/ss;uint64_t q=pmm_alloc_page();if(!q)return-5;uint8_t*t=(uint8_t*)(uintptr_t)q;size_t done=0;for(uint64_t ls=first;ls<=last;ls++){uint64_t ps;if(extent_map(&in,ls,&ps)||ps>=fs->super.total_sectors){pmm_free_page(q);return-6;}if(bio_read(fs->device,ps,1,t,ss)){pmm_free_page(q);return-7;}uint64_t begin=ls==first?off%ss:0,end=ls==last?((off+(uint64_t)size-1)%ss)+1:ss;size_t n=(size_t)(end-begin);if(write){for(size_t i=0;i<n;i++)t[begin+i]=((const uint8_t*)buf)[done+i];if(bio_write(fs->device,ps,1,t,ss)){pmm_free_page(q);return-8;}}else for(size_t i=0;i<n;i++)((uint8_t*)buf)[done+i]=t[begin+i];done+=n;}pmm_free_page(q);return 0;}
int rixfs_read(rixfs_t*fs,uint64_t ino,uint64_t off,void*buf,size_t size){return io_file(fs,ino,off,buf,size,0);}
int rixfs_write(rixfs_t*fs,uint64_t ino,uint64_t off,const void*buf,size_t size){return io_file(fs,ino,off,(void*)buf,size,1);}
int rixfs_sync(rixfs_t*fs){if(!fs||!fs->mounted)return-1;return bio_flush(fs->device);}
void rixfs_unmount(rixfs_t*fs){if(!fs)return;fs->mounted=0;fs->device=NULL;for(size_t i=0;i<sizeof(fs->super);i++)((uint8_t*)&fs->super)[i]=0;}
