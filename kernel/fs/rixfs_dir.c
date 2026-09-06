#include "rixfs_dir.h"
#include <stddef.h>
#include <stdint.h>

#define RIXFS_PAGE_SIZE 4096u

static int name_valid(const char *name, size_t *len_out){
    if(!name||!len_out||name[0]==0||name[0]=='/')return -1;
    size_t n=0;
    while(name[n]){
        if(name[n]=='/')return -1;
        if(++n>RIXFS_NAME_MAX)return -1;
    }
    *len_out=n;
    return 0;
}

static int dir_read_bytes(rixfs_t *fs,uint64_t inode,uint64_t off,void *buf,size_t size){
    return rixfs_read(fs,inode,off,buf,size);
}

int rixfs_lookup_name(rixfs_t *fs,uint64_t dir_inode,const char *name,uint64_t *out_inode,uint8_t *out_type){
    if(!fs||!fs->mounted||!out_inode||!out_type)return -1;
    size_t wanted=0;
    if(name_valid(name,&wanted)!=0)return -2;
    rixfs_inode_disk_t dir;
    if(rixfs_read_inode(fs,dir_inode,&dir)!=0)return -3;
    if(dir.inode!=dir_inode||(dir.mode&0xF000u)!=0x4000u)return -4;
    uint8_t header_buf[16];
    uint64_t off=0;
    while(off<dir.size){
        if(dir.size-off<RIXFS_DIRENT_MIN_SIZE)return -5;
        if(dir_read_bytes(fs,dir_inode,off,header_buf,sizeof(header_buf))!=0)return -6;
        rixfs_dirent_disk_t e;
        const uint8_t *h=header_buf;
        e.inode=(uint64_t)h[0]|((uint64_t)h[1]<<8)|((uint64_t)h[2]<<16)|((uint64_t)h[3]<<24)|((uint64_t)h[4]<<32)|((uint64_t)h[5]<<40)|((uint64_t)h[6]<<48)|((uint64_t)h[7]<<56);
        e.record_size=(uint16_t)h[8]|((uint16_t)h[9]<<8);
        e.type=h[10];e.name_length=h[11];
        e.flags=(uint32_t)h[12]|((uint32_t)h[13]<<8)|((uint32_t)h[14]<<16)|((uint32_t)h[15]<<24);
        if(e.record_size<RIXFS_DIRENT_MIN_SIZE||e.record_size>dir.size-off||e.name_length==0||e.name_length>RIXFS_NAME_MAX)return -7;
        if((uint32_t)RIXFS_DIRENT_MIN_SIZE+(uint32_t)e.name_length>e.record_size)return -8;
        if(e.inode!=0&&e.name_length==wanted){
            char candidate[RIXFS_NAME_MAX+1];
            if(dir_read_bytes(fs,dir_inode,off+RIXFS_DIRENT_MIN_SIZE,candidate,wanted)!=0)return -9;
            size_t i=0;for(;i<wanted;i++)if(candidate[i]!=name[i])break;
            if(i==wanted){*out_inode=e.inode;*out_type=e.type;return e.inode?0:-10;}
        }
        off+=e.record_size;
    }
    return -11;
}

int rixfs_readdir(rixfs_t *fs,uint64_t dir_inode,uint64_t *offset,rixfs_dirent_disk_t *out,char *name,size_t name_capacity){
    if(!fs||!fs->mounted||!offset||!out||!name||name_capacity<2)return -1;
    rixfs_inode_disk_t dir;
    if(rixfs_read_inode(fs,dir_inode,&dir)!=0)return -2;
    if(dir.inode!=dir_inode||(dir.mode&0xF000u)!=0x4000u)return -3;
    uint64_t off=*offset;
    if(off>=dir.size)return 1;
    uint8_t header_buf[RIXFS_DIRENT_MIN_SIZE];
    if(dir.size-off<RIXFS_DIRENT_MIN_SIZE||dir_read_bytes(fs,dir_inode,off,header_buf,sizeof(header_buf))!=0)return -4;
    const uint8_t *h=header_buf;
    out->inode=(uint64_t)h[0]|((uint64_t)h[1]<<8)|((uint64_t)h[2]<<16)|((uint64_t)h[3]<<24)|((uint64_t)h[4]<<32)|((uint64_t)h[5]<<40)|((uint64_t)h[6]<<48)|((uint64_t)h[7]<<56);
    out->record_size=(uint16_t)h[8]|((uint16_t)h[9]<<8);
    out->type=h[10];out->name_length=h[11];
    out->flags=(uint32_t)h[12]|((uint32_t)h[13]<<8)|((uint32_t)h[14]<<16)|((uint32_t)h[15]<<24);
    if(out->record_size<RIXFS_DIRENT_MIN_SIZE||out->record_size>dir.size-off||out->name_length==0||out->name_length>RIXFS_NAME_MAX)return -5;
    if((uint32_t)RIXFS_DIRENT_MIN_SIZE+(uint32_t)out->name_length>out->record_size)return -6;
    if((size_t)out->name_length+1>name_capacity)return -7;
    if(dir_read_bytes(fs,dir_inode,off+RIXFS_DIRENT_MIN_SIZE,name,out->name_length)!=0)return -8;
    name[out->name_length]=0;
    *offset=off+out->record_size;
    return 0;
}
