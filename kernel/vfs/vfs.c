#include "vfs.h"
#include "../fs/rixfs.h"
#include "../fs/rixfs_dir.h"
#include "../storage/block.h"
#include <stddef.h>
#include <stdint.h>
#define VFS_MAX_MOUNTS 4u
typedef struct { rix_vnode_t node; } vfs_root_t;
typedef struct { rixfs_t fs; char path[8]; uint8_t active; } vfs_mount_t;
static vfs_root_t root;static vfs_mount_t mounts[VFS_MAX_MOUNTS];static rix_vnode_t path_node;
static int append_component(char*out,size_t cap,size_t*len,const char*start,size_t n){if(n==0)return 0;if(*len&&out[*len-1]!='/'){if(*len+1>=cap)return-1;out[(*len)++]='/';}if(*len+n>=cap)return-1;for(size_t i=0;i<n;i++)out[(*len)++]=start[i];return 0;}
int vfs_normalize_path(const char*input,char*output,size_t cap){if(!input||!output||cap<2)return-1;size_t len=0;output[len++]='/';const char*p=input;while(*p){while(*p=='/')p++;if(!*p)break;const char*s=p;while(*p&&*p!='/')p++;size_t n=(size_t)(p-s);if(n==1&&s[0]=='.')continue;if(n==2&&s[0]=='.'&&s[1]=='.'){if(len>1){if(output[len-1]=='/')len--;while(len>1&&output[len-1]!='/')len--;}continue;}if(n>RIX_VFS_NAME_MAX||append_component(output,cap,&len,s,n))return-1;}if(len>1&&output[len-1]=='/')len--;output[len]=0;return 0;}
static rix_vfs_type_t dirent_type(uint8_t t){if(t==RIXFS_DIR_TYPE_DIR)return RIX_VFS_DIR;if(t==RIXFS_DIR_TYPE_FILE)return RIX_VFS_FILE;if(t==RIXFS_DIR_TYPE_SYMLINK)return RIX_VFS_SYMLINK;return RIX_VFS_DEVICE;}
static int lookup_rixfs_path(const char*n,rix_vfs_path_t*out){if(!n||!out||!mounts[0].active)return-1;uint64_t ino=mounts[0].fs.super.root_inode;if(n[0]=='/'&&!n[1]){path_node=root.node;path_node.inode=ino;path_node.type=RIX_VFS_DIR;path_node.mode=0755;out->node=&path_node;return 0;}const char*p=n+1;char c[RIX_VFS_NAME_MAX+1];while(*p){const char*s=p;while(*p&&*p!='/')p++;size_t len=(size_t)(p-s);if(!len||len>RIX_VFS_NAME_MAX)return-1;for(size_t i=0;i<len;i++)c[i]=s[i];c[len]=0;uint64_t next;uint8_t t;if(rixfs_lookup_name(&mounts[0].fs,ino,c,&next,&t))return-1;rixfs_inode_disk_t in;if(rixfs_read_inode(&mounts[0].fs,next,&in))return-1;ino=next;while(*p=='/')p++;if(!*p){path_node.inode=ino;path_node.type=dirent_type(t);path_node.mode=in.mode;path_node.uid=in.uid;path_node.gid=in.gid;path_node.size=in.size;out->node=&path_node;return 0;}if((in.mode&RIXFS_IFMT)!=RIXFS_IFDIR)return-1;}return-1;}
int vfs_init(void){root.node.inode=1;root.node.type=RIX_VFS_DIR;root.node.mode=0755;root.node.uid=0;root.node.gid=0;root.node.size=0;for(size_t i=0;i<VFS_MAX_MOUNTS;i++)mounts[i].active=0;return 0;}
int vfs_mount_root(rix_block_device_t*d){if(!d||mounts[0].active)return-1;int r=rixfs_mount(d,&mounts[0].fs);if(r)return r;mounts[0].active=1;mounts[0].path[0]='/';mounts[0].path[1]=0;root.node.inode=mounts[0].fs.super.root_inode;return 0;}
int vfs_unmount_root(void){if(!mounts[0].active)return-1;rixfs_unmount(&mounts[0].fs);mounts[0].active=0;root.node.inode=1;return 0;}
int vfs_root(rix_vfs_path_t*out){if(!out)return-1;out->node=&root.node;out->path[0]='/';out->path[1]=0;return 0;}
int vfs_lookup(const char*path,rix_vfs_path_t*out){if(!path||!out)return-1;if(vfs_normalize_path(path,out->path,sizeof(out->path)))return-1;if(out->path[0]=='/'&&!out->path[1])return vfs_root(out);return lookup_rixfs_path(out->path,out);}
int vfs_lookup_from(const rix_vfs_path_t*base,const char*path,rix_vfs_path_t*out){if(!base||!path||!out)return-1;if(path[0]=='/')return vfs_lookup(path,out);char joined[RIX_VFS_PATH_MAX];size_t len=0;for(size_t i=0;base->path[i];i++){if(len+1>=sizeof(joined))return-1;joined[len++]=base->path[i];}if(len==0||joined[len-1]!='/'){if(len+1>=sizeof(joined))return-1;joined[len++]='/';}for(size_t i=0;path[i];i++){if(len+1>=sizeof(joined))return-1;joined[len++]=path[i];}joined[len]=0;return vfs_lookup(joined,out);}
rixfs_t *vfs_root_fs(void){return mounts[0].active?&mounts[0].fs:(rixfs_t*)0;}
