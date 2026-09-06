#include "vfs.h"
#include "../fs/rixfs.h"
#include "../fs/rixfs_dir.h"
#include "../storage/block.h"
#include <stddef.h>
#include <stdint.h>

#define VFS_MAX_MOUNTS 4u

typedef struct { rix_vnode_t node; } vfs_root_t;
typedef struct { rixfs_t fs; char path[8]; uint8_t active; } vfs_mount_t;
static vfs_root_t root;
static vfs_mount_t mounts[VFS_MAX_MOUNTS];
static rix_vnode_t path_node;

static int append_component(char *out,size_t cap,size_t *len,const char *start,size_t n){
    if(n==0)return 0;
    if(*len&&out[*len-1]!='/'){if(*len+1>=cap)return -1;out[(*len)++]='/';}
    if(*len+n>=cap)return -1;
    for(size_t i=0;i<n;i++)out[(*len)++]=start[i];
    return 0;
}

int vfs_normalize_path(const char *input,char *output,size_t output_size){
    if(!input||!output||output_size<2)return -1;
    size_t len=0;output[len++]='/';
    const char *p=input;
    while(*p){
        while(*p=='/')p++;
        if(!*p)break;
        const char *s=p;while(*p&&*p!='/')p++;size_t n=(size_t)(p-s);
        if(n==1&&s[0]=='.')continue;
        if(n==2&&s[0]=='.'&&s[1]=='.'){
            if(len>1){if(output[len-1]=='/')len--;while(len>1&&output[len-1]!='/')len--;}
            continue;
        }
        if(n>RIX_VFS_NAME_MAX)return -1;
        if(append_component(output,output_size,&len,s,n)!=0)return -1;
    }
    if(len>1&&output[len-1]=='/')len--;output[len]=0;return 0;
}

static rix_vfs_type_t dirent_type(uint8_t type){
    if(type==RIXFS_DIR_TYPE_DIR)return RIX_VFS_DIR;
    if(type==RIXFS_DIR_TYPE_FILE)return RIX_VFS_FILE;
    if(type==RIXFS_DIR_TYPE_SYMLINK)return RIX_VFS_SYMLINK;
    return RIX_VFS_DEVICE;
}

static int lookup_rixfs_path(const char *normalized,rix_vfs_path_t *out){
    if(!normalized||!out||!mounts[0].active)return -1;
    uint64_t inode=mounts[0].fs.super.root_inode;
    if(normalized[0]=='/'&&normalized[1]==0){
        path_node=root.node;path_node.inode=inode;path_node.type=RIX_VFS_DIR;out->node=&path_node;return 0;
    }
    const char *p=normalized+1;
    char component[RIX_VFS_NAME_MAX+1];
    while(*p){
        const char *s=p;while(*p&&*p!='/')p++;size_t n=(size_t)(p-s);
        if(n==0||n>RIX_VFS_NAME_MAX)return -1;
        for(size_t i=0;i<n;i++)component[i]=s[i];component[n]=0;
        uint64_t next=0;uint8_t type=0;
        if(rixfs_lookup_name(&mounts[0].fs,inode,component,&next,&type)!=0)return -1;
        rixfs_inode_disk_t in;
        if(rixfs_read_inode(&mounts[0].fs,next,&in)!=0)return -1;
        inode=next;
        while(*p=='/')p++;
        if(*p==0){
            path_node.inode=inode;path_node.type=dirent_type(type);path_node.mode=in.mode;path_node.uid=in.uid;path_node.gid=in.gid;path_node.size=in.size;out->node=&path_node;return 0;
        }
        if((in.mode&0xF000u)!=0x4000u)return -1;
    }
    return -1;
}

int vfs_init(void){
    root.node.inode=1;root.node.type=RIX_VFS_DIR;root.node.mode=0755;root.node.uid=0;root.node.gid=0;root.node.size=0;
    for(size_t i=0;i<VFS_MAX_MOUNTS;i++)mounts[i].active=0;
    return 0;
}

int vfs_mount_root(rix_block_device_t *device){
    if(!device||mounts[0].active)return -1;
    int rc=rixfs_mount(device,&mounts[0].fs);
    if(rc!=0)return rc;
    mounts[0].active=1;
    mounts[0].path[0]='/';mounts[0].path[1]=0;
    root.node.inode=mounts[0].fs.super.root_inode;
    return 0;
}

int vfs_unmount_root(void){
    if(!mounts[0].active)return -1;
    rixfs_unmount(&mounts[0].fs);mounts[0].active=0;
    root.node.inode=1;root.node.type=RIX_VFS_DIR;root.node.mode=0755;root.node.uid=0;root.node.gid=0;root.node.size=0;
    return 0;
}

int vfs_root(rix_vfs_path_t *out){if(!out)return -1;out->node=&root.node;out->path[0]='/';out->path[1]=0;return 0;}
int vfs_lookup(const char *path,rix_vfs_path_t *out){if(!path||!out)return -1;if(vfs_normalize_path(path,out->path,sizeof(out->path))!=0)return -1;if(out->path[0]=='/'&&out->path[1]==0)return vfs_root(out);return lookup_rixfs_path(out->path,out);}
int vfs_lookup_from(const rix_vfs_path_t *base,const char *path,rix_vfs_path_t *out){
    if(!base||!path||!out)return -1;
    if(path[0]=='/')return vfs_lookup(path,out);
    char joined[RIX_VFS_PATH_MAX];size_t len=0;
    for(size_t i=0;base->path[i];i++){if(len+1>=sizeof(joined))return -1;joined[len++]=base->path[i];}
    if(len==0||joined[len-1]!='/'){if(len+1>=sizeof(joined))return -1;joined[len++]='/';}
    for(size_t i=0;path[i];i++){if(len+1>=sizeof(joined))return -1;joined[len++]=path[i];}
    joined[len]=0;
    return vfs_lookup(joined,out);
}
