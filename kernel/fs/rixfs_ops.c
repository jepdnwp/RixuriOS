#include "rixfs.h"
#include "rixfs_dir.h"
#include "../mm/pmm.h"
#include <stddef.h>
#include <stdint.h>
#define RIXFS_JOURNAL_MAGIC 0x524A4E4C56310001ULL
static int rd(rix_block_device_t*d,uint64_t s,void*b){rix_bio_t x={0};x.op=RIX_BIO_READ;x.sector=s;x.count=1;x.buffer=b;x.buffer_size=d->sector_size;return block_submit(d,&x);}
static int wr(rix_block_device_t*d,uint64_t s,const void*b){rix_bio_t x={0};x.op=RIX_BIO_WRITE;x.sector=s;x.count=1;x.buffer=(void*)b;x.buffer_size=d->sector_size;return block_submit(d,&x);}
static int flush(rix_block_device_t*d){rix_bio_t x={0};x.op=RIX_BIO_FLUSH;return block_submit(d,&x);}
static uint64_t hash64(const void*p,size_t n){const uint8_t*b=p;uint64_t h=1469598103934665603ULL;for(size_t i=0;i<n;i++)h=(h^b[i])*1099511628211ULL;return h;}
static int journal_write(rixfs_t*f,uint64_t target,const void*data){if(!f||!f->mounted||f->super.journal_sectors<2||target>=f->super.total_sectors||target==f->super.journal_sector||target==f->super.journal_sector+1)return-1;uint64_t q=pmm_alloc_page();if(!q)return-2;uint8_t*b=(uint8_t*)(uintptr_t)q;for(uint32_t i=0;i<f->device->sector_size;i++)b[i]=((const uint8_t*)data)[i];uint64_t sum=hash64(b,f->device->sector_size);if(wr(f->device,f->super.journal_sector+1,b)||flush(f->device)){pmm_free_page(q);return-3;}for(uint32_t i=0;i<f->device->sector_size;i++)b[i]=0;uint64_t magic=RIXFS_JOURNAL_MAGIC,seq=++f->super.journal_generation;for(unsigned i=0;i<8;i++){b[i]=(uint8_t)(magic>>(8*i));b[8+i]=(uint8_t)(target>>(8*i));b[16+i]=(uint8_t)(seq>>(8*i));b[32+i]=(uint8_t)(sum>>(8*i));}if(wr(f->device,f->super.journal_sector,b)||flush(f->device)){pmm_free_page(q);return-4;}for(uint32_t i=0;i<f->device->sector_size;i++)b[i]=((const uint8_t*)data)[i];if(wr(f->device,target,b)||flush(f->device)){pmm_free_page(q);return-5;}for(uint32_t i=0;i<f->device->sector_size;i++)b[i]=0;int r=wr(f->device,f->super.journal_sector,b);if(!r)r=flush(f->device);pmm_free_page(q);return r;}
static int inode_pos(const rixfs_t*f,uint64_t ino,uint64_t*s,uint32_t*o){if(!f||!f->mounted||!ino||ino>f->super.inode_count)return-1;uint64_t x=(ino-1)*RIXFS_INODE_SIZE;*s=f->super.inode_table_sector+x/f->device->sector_size;*o=(uint32_t)(x%f->device->sector_size);return*o+RIXFS_INODE_SIZE<=f->device->sector_size?0:-2;}
int rixfs_write_inode(rixfs_t*f,uint64_t ino,const rixfs_inode_disk_t*in){uint64_t s;uint32_t o;if(!in||inode_pos(f,ino,&s,&o))return-1;uint64_t q=pmm_alloc_page();if(!q)return-2;uint8_t*b=(uint8_t*)(uintptr_t)q;int r=rd(f->device,s,b);if(!r){for(size_t i=0;i<sizeof(*in);i++)b[o+i]=((const uint8_t*)in)[i];r=journal_write(f,s,b);}pmm_free_page(q);return r;}
static int bit(rixfs_t*f,uint64_t sec,int set){if(!f||!f->mounted||sec>=f->super.total_sectors)return-1;uint64_t bs=f->device->sector_size,x=sec/8,bsec=f->super.bitmap_sector+x/bs,off=x%bs;if(bsec>=f->super.bitmap_sector+f->super.bitmap_sectors)return-2;uint64_t q=pmm_alloc_page();if(!q)return-3;uint8_t*b=(uint8_t*)(uintptr_t)q;int r=rd(f->device,bsec,b);if(!r){uint8_t m=(uint8_t)(1u<<(sec%8));if(set)b[off]|=m;else b[off]&=(uint8_t)~m;r=journal_write(f,bsec,b);}pmm_free_page(q);return r;}
static int bit_get(rixfs_t*f,uint64_t sec,int*used){uint64_t bs=f->device->sector_size,x=sec/8,bsec=f->super.bitmap_sector+x/bs,off=x%bs;if(bsec>=f->super.bitmap_sector+f->super.bitmap_sectors)return-1;uint64_t q=pmm_alloc_page();if(!q)return-2;uint8_t*b=(uint8_t*)(uintptr_t)q;int r=rd(f->device,bsec,b);if(!r)*used=(b[off]&(1u<<(sec%8)))!=0;pmm_free_page(q);return r;}
static int alloc_sec(rixfs_t*f,uint64_t*out){uint64_t s=f->super.free_hint;if(s<f->super.data_start_sector)s=f->super.data_start_sector;for(unsigned pass=0;pass<2;pass++){uint64_t a=pass?f->super.data_start_sector:s,z=pass?s:f->super.total_sectors;for(;a<z;a++){int used=0;if(bit_get(f,a,&used))return-1;if(!used){if(bit(f,a,1))return-2;f->super.free_hint=a+1;*out=a;return 0;}}}return-3;}
static int append_extent(rixfs_inode_disk_t*in,uint64_t s){for(unsigned i=0;i<RIXFS_DIRECT_EXTENTS;i++){if(in->extent_length[i]){uint64_t end=in->extent_start[i]+in->extent_length[i];if(end==s){in->extent_length[i]++;return 0;}}else{in->extent_start[i]=s;in->extent_length[i]=1;return 0;}}return-1;}
static int free_inode_data(rixfs_t*f,const rixfs_inode_disk_t*in){for(unsigned e=0;e<RIXFS_DIRECT_EXTENTS;e++)for(uint64_t j=0;j<in->extent_length[e];j++)if(bit(f,in->extent_start[e]+j,0))return-1;return 0;}
static int grow(rixfs_t*f,rixfs_inode_disk_t*in,uint64_t bytes){uint64_t ss=f->device->sector_size;if(bytes==0){in->size=0;return 0;}uint64_t old=(in->size+ss-1)/ss,neu=(bytes+ss-1)/ss,added=0;for(uint64_t i=old;i<neu;i++){uint64_t s;if(alloc_sec(f,&s)){for(unsigned e=0;e<RIXFS_DIRECT_EXTENTS;e++)for(uint64_t j=0;j<in->extent_length[e]&&added;j++){uint64_t sec=in->extent_start[e]+in->extent_length[e]-1-j;if(sec>=f->super.data_start_sector){bit(f,sec,0);added--;}}return-1;}if(append_extent(in,s)){bit(f,s,0);for(unsigned e=0;e<RIXFS_DIRECT_EXTENTS;e++)for(uint64_t j=0;j<in->extent_length[e]&&added;j++){uint64_t sec=in->extent_start[e]+in->extent_length[e]-1-j;if(sec>=f->super.data_start_sector){bit(f,sec,0);added--;}}return-2;}added++;}in->size=bytes;return 0;}
int rixfs_truncate(rixfs_t*f,uint64_t ino,uint64_t ns){if(!f||!f->mounted)return-1;rixfs_inode_disk_t in;if(rixfs_read_inode(f,ino,&in))return-2;if((in.mode&RIXFS_IFMT)!=RIXFS_IFREG)return-3;uint64_t ss=f->device->sector_size;if(ns>in.size){if(grow(f,&in,ns))return-4;return rixfs_write_inode(f,ino,&in);}uint64_t keep=(ns+ss-1)/ss,seen=0;for(unsigned e=0;e<RIXFS_DIRECT_EXTENTS;e++){uint64_t n=in.extent_length[e];if(!n)continue;if(seen>=keep){for(uint64_t j=0;j<n;j++)if(bit(f,in.extent_start[e]+j,0))return-5;in.extent_start[e]=0;in.extent_length[e]=0;}else if(seen+n>keep){uint64_t cut=seen+n-keep;for(uint64_t j=0;j<cut;j++)if(bit(f,in.extent_start[e]+n-1-j,0))return-6;in.extent_length[e]-=cut;}seen+=n;}in.size=ns;return rixfs_write_inode(f,ino,&in);}
static int valid_name(const char*n,size_t*l){if(!n||!n[0]||n[0]=='/'||(n[0]=='.'&&n[1]==0)||(n[0]=='.'&&n[1]=='.'&&n[2]==0))return-1;size_t x=0;while(n[x]){if(n[x]=='/'||++x>RIXFS_NAME_MAX)return-1;}*l=x;return 0;}
static int alloc_inode(rixfs_t*f,uint64_t*out){for(uint64_t i=1;i<=f->super.inode_count;i++){rixfs_inode_disk_t in;if(rixfs_read_inode(f,i,&in))return-1;if(in.inode==0){*out=i;return 0;}}return-2;}
static int dir_sector(const rixfs_inode_disk_t*dir,uint64_t logical,uint64_t*sector){uint64_t pos=logical;for(unsigned e=0;e<RIXFS_DIRECT_EXTENTS;e++){if(pos<dir->extent_length[e]){*sector=dir->extent_start[e]+pos;return 0;}pos-=dir->extent_length[e];}return-1;}
static int dir_append(rixfs_t*f,rixfs_inode_disk_t*dir,uint64_t ino,uint8_t type,const char*name,size_t nl){uint64_t ss=f->device->sector_size;if(ss>UINT16_MAX||nl>RIXFS_NAME_MAX||dir->size%ss)return-1;uint64_t sectors=dir->size/ss,sec=0;int existing=0,add_size=0;uint64_t q=pmm_alloc_page();if(!q)return-2;uint8_t*b=(uint8_t*)(uintptr_t)q;for(uint64_t logical=0;logical<sectors;logical++){if(dir_sector(dir,logical,&sec)||rd(f->device,sec,b)){pmm_free_page(q);return-3;}uint64_t old=0;for(unsigned i=0;i<8;i++)old|=(uint64_t)b[i]<<(8*i);if(!old){existing=1;break;}}if(!existing&&dir_sector(dir,sectors,&sec)==0){existing=1;add_size=sectors==0;}if(!existing){if(alloc_sec(f,&sec)){pmm_free_page(q);return-4;}if(append_extent(dir,sec)){bit(f,sec,0);pmm_free_page(q);return-5;}
add_size=1;}int r=existing?rd(f->device,sec,b):0;if(r){pmm_free_page(q);return-6;}if(!existing)for(uint32_t i=0;i<ss;i++)b[i]=0;uint16_t rs=(uint16_t)ss;for(unsigned i=0;i<8;i++)b[i]=(uint8_t)(ino>>(8*i));b[8]=(uint8_t)rs;b[9]=(uint8_t)(rs>>8);b[10]=type;b[11]=(uint8_t)nl;for(size_t i=0;i<nl;i++)b[16+i]=name[i];r=journal_write(f,sec,b);pmm_free_page(q);if(r){if(!existing)bit(f,sec,0);return-7;}if(add_size)dir->size+=ss;return 0;}
int rixfs_mkdir(rixfs_t*f,uint64_t d,const char*name,uint32_t mode,uint32_t uid,uint32_t gid,uint64_t*out){size_t nl;if(!f||!out||valid_name(name,&nl))return-1;uint64_t old;uint8_t t;if(rixfs_lookup_name(f,d,name,&old,&t)==0)return-2;uint64_t ino;if(alloc_inode(f,&ino))return-3;uint64_t sec;if(alloc_sec(f,&sec))return-4;rixfs_inode_disk_t in={0};in.inode=ino;in.mode=RIXFS_IFDIR|(mode&07777u);in.uid=uid;in.gid=gid;in.generation=1;in.links=1;in.extent_start[0]=sec;in.extent_length[0]=1;if(rixfs_write_inode(f,ino,&in)){bit(f,sec,0);return-5;}rixfs_inode_disk_t dir;if(rixfs_read_inode(f,d,&dir)||dir_append(f,&dir,ino,RIXFS_DIR_TYPE_DIR,name,nl)){free_inode_data(f,&in);rixfs_inode_disk_t z={0};rixfs_write_inode(f,ino,&z);return-6;}if(rixfs_write_inode(f,d,&dir))return-7;*out=ino;return 0;}
int rixfs_create(rixfs_t*f,uint64_t d,const char*name,uint32_t mode,uint32_t uid,uint32_t gid,uint64_t*out){size_t nl;if(!f||!out||valid_name(name,&nl))return-1;uint64_t old;uint8_t t;if(rixfs_lookup_name(f,d,name,&old,&t)==0)return-2;uint64_t ino;if(alloc_inode(f,&ino))return-3;rixfs_inode_disk_t in={0};in.inode=ino;in.mode=RIXFS_IFREG|(mode&07777u);in.uid=uid;in.gid=gid;in.generation=1;in.links=1;if(rixfs_write_inode(f,ino,&in))return-4;rixfs_inode_disk_t dir;if(rixfs_read_inode(f,d,&dir)||dir_append(f,&dir,ino,RIXFS_DIR_TYPE_FILE,name,nl)){rixfs_inode_disk_t z={0};rixfs_write_inode(f,ino,&z);return-5;}if(rixfs_write_inode(f,d,&dir))return-6;*out=ino;return 0;}
int rixfs_link(rixfs_t*f,uint64_t source,uint64_t d,const char*name,uint8_t type){size_t nl;if(!f||valid_name(name,&nl))return-1;uint64_t old;uint8_t t;if(rixfs_lookup_name(f,d,name,&old,&t)==0)return-2;rixfs_inode_disk_t in;if(rixfs_read_inode(f,source,&in)||!in.inode)return-3;if((in.mode&RIXFS_IFMT)==RIXFS_IFDIR)return-4;if(in.links==UINT32_MAX)return-5;rixfs_inode_disk_t dir;if(rixfs_read_inode(f,d,&dir)||dir_append(f,&dir,source,type,name,nl))return-6;if(rixfs_write_inode(f,d,&dir))return-7;in.links=in.links?in.links+1u:2u;return rixfs_write_inode(f,source,&in);}
int rixfs_unlink(rixfs_t*f,uint64_t d,const char*name){size_t nl;if(!f||valid_name(name,&nl))return-1;uint64_t ino;uint8_t type;if(rixfs_lookup_name(f,d,name,&ino,&type))return-2;if(ino==f->super.root_inode)return-3;rixfs_inode_disk_t in;if(rixfs_read_inode(f,ino,&in))return-4;if((in.mode&RIXFS_IFMT)==RIXFS_IFDIR)return-5;if(rixfs_remove_name(f,d,name))return-6;if(in.links>1){in.links--;return rixfs_write_inode(f,ino,&in);}if(free_inode_data(f,&in))return-7;rixfs_inode_disk_t z={0};return rixfs_write_inode(f,ino,&z);}
int rixfs_rename(rixfs_t*f,uint64_t d,const char*old_name,const char*new_name){
    size_t old_len,new_len;if(!f||!f->mounted||valid_name(old_name,&old_len)||valid_name(new_name,&new_len))return-1;
    if(old_len==new_len){size_t i=0;for(;i<old_len&&old_name[i]==new_name[i];i++){}if(i==old_len)return 0;}
    uint64_t old_ino;uint8_t old_type;if(rixfs_lookup_name(f,d,old_name,&old_ino,&old_type))return-2;
    uint64_t existing;if(rixfs_lookup_name(f,d,new_name,&existing,&old_type)==0)return-17;
    rixfs_inode_disk_t dir;if(rixfs_read_inode(f,d,&dir)||dir.inode!=d||(dir.mode&RIXFS_IFMT)!=RIXFS_IFDIR)return-3;
    uint64_t off=0;rixfs_dirent_disk_t entry;char name[RIXFS_NAME_MAX+1];uint64_t start=0;int found=0;
    while(off<dir.size){start=off;int rc=rixfs_readdir(f,d,&off,&entry,name,sizeof(name));if(rc==1)break;if(rc!=0)return-4;
        if(entry.inode==old_ino&&entry.type==old_type&&entry.name_length==old_len){size_t i=0;for(;i<old_len&&name[i]==old_name[i];i++){}if(i==old_len){found=1;break;}}
    }
    if(!found||entry.record_size!=f->device->sector_size||start%f->device->sector_size)return-5;
    uint64_t logical=start/f->device->sector_size,physical=0;if(dir_sector(&dir,logical,&physical))return-6;
    uint64_t page=pmm_alloc_page();if(!page)return-7;uint8_t*b=(uint8_t*)(uintptr_t)page;int rc=rd(f->device,physical,b);
    if(!rc){b[11]=(uint8_t)new_len;for(size_t i=0;i<new_len;i++)b[16+i]=(uint8_t)new_name[i];for(size_t i=new_len;i<old_len;i++)b[16+i]=0;rc=journal_write(f,physical,b);}pmm_free_page(page);return rc;
}
static int directory_empty(rixfs_t*f,uint64_t ino){uint64_t off=0;rixfs_dirent_disk_t e;char name[RIXFS_NAME_MAX+1];for(;;){int r=rixfs_readdir(f,ino,&off,&e,name,sizeof(name));if(r==1)return 0;if(r!=0)return -1;return -2;}}
int rixfs_rmdir(rixfs_t*f,uint64_t d,const char*name){size_t nl;if(!f||valid_name(name,&nl))return-1;uint64_t ino;uint8_t type;if(rixfs_lookup_name(f,d,name,&ino,&type))return-2;if(ino==f->super.root_inode)return-3;rixfs_inode_disk_t in;if(rixfs_read_inode(f,ino,&in))return-4;if((in.mode&RIXFS_IFMT)!=RIXFS_IFDIR)return-5;if(directory_empty(f,ino))return-6;if(free_inode_data(f,&in))return-7;if(rixfs_remove_name(f,d,name))return-8;rixfs_inode_disk_t z={0};return rixfs_write_inode(f,ino,&z);}
int rixfs_format(rix_block_device_t*d,uint64_t ic){if(!d||d->sector_size<512||d->sector_size>RIXFS_SECTOR_MAX||d->sector_count<128||ic<8)return-1;uint64_t is=(ic*RIXFS_INODE_SIZE+d->sector_size-1)/d->sector_size,bs=(d->sector_count+8*d->sector_size-1)/(8*d->sector_size),js=1+is+bs,ds=js+2;if(ds+1>=d->sector_count)return-2;uint64_t q=pmm_alloc_page();if(!q)return-3;uint8_t*b=(uint8_t*)(uintptr_t)q;for(uint32_t i=0;i<d->sector_size;i++)b[i]=0;rixfs_superblock_t sb={0};sb.magic=RIXFS_MAGIC;sb.version=RIXFS_VERSION;sb.header_size=sizeof(sb);sb.sector_size=d->sector_size;sb.total_sectors=d->sector_count;sb.inode_table_sector=1;sb.inode_count=ic;sb.bitmap_sector=1+is;sb.bitmap_sectors=bs;sb.journal_sector=js;sb.journal_sectors=2;sb.data_start_sector=ds;sb.root_inode=1;sb.generation=1;sb.free_hint=ds;sb.checksum=hash64(&sb,sizeof(sb));for(size_t i=0;i<sizeof(sb);i++)b[i]=((const uint8_t*)&sb)[i];if(wr(d,0,b)){pmm_free_page(q);return-4;}for(uint64_t s=1;s<ds;s++){for(uint32_t i=0;i<d->sector_size;i++)b[i]=0;if(wr(d,s,b)){pmm_free_page(q);return-5;}}rixfs_t f={.device=d,.super=sb,.mounted=1};for(uint64_t s=0;s<ds;s++)if(bit(&f,s,1)){pmm_free_page(q);return-6;}uint64_t root;if(alloc_sec(&f,&root)){pmm_free_page(q);return-7;}rixfs_inode_disk_t in={0};in.inode=1;in.mode=RIXFS_IFDIR|0755u;in.generation=1;in.extent_start[0]=root;in.extent_length[0]=1;if(rixfs_write_inode(&f,1,&in)){pmm_free_page(q);return-8;}for(uint32_t i=0;i<d->sector_size;i++)b[i]=0;if(wr(d,root,b)){pmm_free_page(q);return-9;}pmm_free_page(q);return 0;}
int rixfs_format_standard_tree(rix_block_device_t*d,uint64_t ic){if(rixfs_format(d,ic)!=0)return-1;rixfs_t f={0};if(rixfs_mount(d,&f)!=0)return-2;static const char*dirs[]={"boot","bin","sbin","lib","lib64","usr","etc","home","root","var","tmp","dev","proc","sys","run","opt","mnt","media"};for(size_t i=0;i<sizeof(dirs)/sizeof(dirs[0]);i++){uint64_t ino=0;if(rixfs_mkdir(&f,f.super.root_inode,dirs[i],0755,0,0,&ino)!=0){rixfs_unmount(&f);return-3;}}if(rixfs_sync(&f)!=0){rixfs_unmount(&f);return-4;}rixfs_unmount(&f);return 0;}


static int acl_valid(const rixfs_acl_t *acl) {
    if (!acl || acl->version != RIXFS_ACL_VERSION) return -1;
    if (acl->user != RIXFS_ACL_NONE && (acl->user_perm & ~RIXFS_ACL_PERM_MASK)) return -2;
    if (acl->group != RIXFS_ACL_NONE && (acl->group_perm & ~RIXFS_ACL_PERM_MASK)) return -3;
    if (acl->mask & ~RIXFS_ACL_PERM_MASK) return -4;
    return 0;
}

int rixfs_get_acl(rixfs_t *f, uint64_t ino, rixfs_acl_t *out) {
    if (!f || !out) return -1;
    rixfs_inode_disk_t in;
    if (rixfs_read_inode(f, ino, &in)) return -2;
    out->version = RIXFS_ACL_VERSION;
    if (!(in.flags & RIXFS_INODE_FLAG_ACL)) {
        out->user = RIXFS_ACL_NONE;
        out->user_perm = 0;
        out->group = RIXFS_ACL_NONE;
        out->group_perm = 0;
        out->mask = RIXFS_ACL_PERM_MASK;
        return 0;
    }
    out->user = in.acl_user;
    out->user_perm = in.acl_user_perm & RIXFS_ACL_PERM_MASK;
    out->group = in.acl_group;
    out->group_perm = in.acl_group_perm & RIXFS_ACL_PERM_MASK;
    out->mask = in.acl_mask & RIXFS_ACL_PERM_MASK;
    return 0;
}

int rixfs_set_acl(rixfs_t *f, uint64_t ino, const rixfs_acl_t *acl) {
    if (acl_valid(acl)) return -1;
    rixfs_inode_disk_t in;
    if (rixfs_read_inode(f, ino, &in)) return -2;
    in.flags |= RIXFS_INODE_FLAG_ACL;
    in.acl_user = acl->user;
    in.acl_user_perm = acl->user_perm & RIXFS_ACL_PERM_MASK;
    in.acl_group = acl->group;
    in.acl_group_perm = acl->group_perm & RIXFS_ACL_PERM_MASK;
    in.acl_mask = acl->mask & RIXFS_ACL_PERM_MASK;
    return rixfs_write_inode(f, ino, &in);
}

int rixfs_clear_acl(rixfs_t *f, uint64_t ino) {
    if (!f) return -1;
    rixfs_inode_disk_t in;
    if (rixfs_read_inode(f, ino, &in)) return -2;
    in.flags &= ~RIXFS_INODE_FLAG_ACL;
    in.acl_user = RIXFS_ACL_NONE;
    in.acl_user_perm = 0;
    in.acl_group = RIXFS_ACL_NONE;
    in.acl_group_perm = 0;
    in.acl_mask = RIXFS_ACL_PERM_MASK;
    return rixfs_write_inode(f, ino, &in);
}
