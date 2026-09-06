#include "vfs.h"
#include "../fs/rixfs.h"
#include "../fs/rixfs_dir.h"
#include "../process/process.h"
#include "../ipc/pipe.h"
#include <stddef.h>
#include <stdint.h>

#define VFS_MAX_MOUNTS 4u

typedef struct { rix_vnode_t node; } vfs_root_t;
typedef struct { rixfs_t fs; char path[8]; uint8_t active; } vfs_mount_t;
typedef struct { uint8_t used; uint8_t type; uint8_t writable; uint8_t append; uint64_t inode; uint64_t offset; rix_pipe_t *pipe; uint8_t pipe_write; } vfs_fd_t;
typedef struct { rix_pipe_t pipe; uint8_t used; uint8_t refs; } vfs_pipe_slot_t;
#define VFS_FD_PIPE_READ 5u
#define VFS_FD_PIPE_WRITE 6u
#define VFS_PIPE_MAX 32u

static vfs_root_t root;
static vfs_mount_t mounts[VFS_MAX_MOUNTS];
static rix_vnode_t path_node;
static vfs_fd_t fds[RIX_PROCESS_MAX][RIX_VFS_FD_MAX];
static vfs_pipe_slot_t pipe_slots[VFS_PIPE_MAX];

static int append_component(char *out,size_t cap,size_t *len,const char *start,size_t n){
    if(!n)return 0;
    if(*len&&out[*len-1]!='/'){if(*len+1>=cap)return -1;out[(*len)++]='/';}
    if(n>=cap-*len)return -1;
    for(size_t i=0;i<n;i++)out[(*len)++]=start[i];
    return 0;
}
int vfs_normalize_path(const char *input,char *output,size_t cap){
    if(!input||!output||cap<2)return -1;
    size_t len=0;output[len++]='/';const char *p=input;
    while(*p){while(*p=='/')p++;if(!*p)break;const char*s=p;while(*p&&*p!='/')p++;size_t n=(size_t)(p-s);
        if(n==1&&s[0]=='.')continue;
        if(n==2&&s[0]=='.'&&s[1]=='.'){if(len>1){if(output[len-1]=='/')len--;while(len>1&&output[len-1]!='/')len--;}continue;}
        if(n>RIX_VFS_NAME_MAX||append_component(output,cap,&len,s,n))return -1;
    }if(len>1&&output[len-1]=='/')len--;output[len]=0;return 0;
}
static rix_vfs_type_t dirent_type(uint8_t t){if(t==RIXFS_DIR_TYPE_DIR)return RIX_VFS_DIR;if(t==RIXFS_DIR_TYPE_FILE)return RIX_VFS_FILE;if(t==RIXFS_DIR_TYPE_SYMLINK)return RIX_VFS_SYMLINK;return RIX_VFS_DEVICE;}
static int pid_slot(uint64_t pid,size_t *slot){if(!slot||pid>=RIX_PROCESS_MAX)return -1;*slot=(size_t)pid;return 0;}
static int lookup_rixfs_path(const char*n,rix_vfs_path_t*out){
    if(!n||!out||!mounts[0].active)return -1;
    uint64_t ino=mounts[0].fs.super.root_inode;
    if(n[0]=='/'&&!n[1]){path_node=root.node;path_node.inode=ino;path_node.type=RIX_VFS_DIR;path_node.mode=0755;out->node=&path_node;return 0;}
    const char*p=n+1;char c[RIX_VFS_NAME_MAX+1];
    while(*p){const char*s=p;while(*p&&*p!='/')p++;size_t len=(size_t)(p-s);if(!len||len>RIX_VFS_NAME_MAX)return -1;for(size_t i=0;i<len;i++)c[i]=s[i];c[len]=0;
        uint64_t next;uint8_t t;if(rixfs_lookup_name(&mounts[0].fs,ino,c,&next,&t))return -1;rixfs_inode_disk_t in;if(rixfs_read_inode(&mounts[0].fs,next,&in))return -1;ino=next;while(*p=='/')p++;
        if(!*p){path_node.inode=ino;path_node.type=dirent_type(t);path_node.mode=in.mode;path_node.uid=in.uid;path_node.gid=in.gid;path_node.size=in.size;out->node=&path_node;return 0;}if((in.mode&RIXFS_IFMT)!=RIXFS_IFDIR)return -1;}
    return -1;
}
static int split_parent(const char*path,char*parent,size_t pc,char*name,size_t nc){
    char norm[RIX_VFS_PATH_MAX];if(vfs_normalize_path(path,norm,sizeof(norm)))return -1;size_t len=0;while(norm[len])len++;if(len<=1)return -1;size_t slash=len;while(slash>0&&norm[slash-1]!='/')slash--;size_t nl=len-slash;if(!nl||nl>=nc)return -1;
    for(size_t i=0;i<nl;i++)name[i]=norm[slash+i];
    name[nl]=0;if(slash==1){if(pc<2)return -1;parent[0]='/';parent[1]=0;return 0;}if(slash>=pc)return -1;for(size_t i=0;i<slash-1;i++)parent[i]=norm[i];parent[slash-1]=0;return 0;
}
int vfs_init(void){root.node.inode=1;root.node.type=RIX_VFS_DIR;root.node.mode=0755;root.node.uid=0;root.node.gid=0;root.node.size=0;for(size_t i=0;i<VFS_MAX_MOUNTS;i++)mounts[i].active=0;for(size_t p=0;p<RIX_PROCESS_MAX;p++)for(size_t f=0;f<RIX_VFS_FD_MAX;f++)fds[p][f].used=0;return 0;}
int vfs_mount_root(rix_block_device_t*d){if(!d||mounts[0].active)return -1;int r=rixfs_mount(d,&mounts[0].fs);if(r)return r;mounts[0].active=1;mounts[0].path[0]='/';mounts[0].path[1]=0;root.node.inode=mounts[0].fs.super.root_inode;return 0;}
int vfs_unmount_root(void){if(!mounts[0].active)return -1;for(size_t p=0;p<RIX_PROCESS_MAX;p++)for(size_t f=0;f<RIX_VFS_FD_MAX;f++)fds[p][f].used=0;rixfs_unmount(&mounts[0].fs);mounts[0].active=0;root.node.inode=1;return 0;}
rixfs_t*vfs_root_fs(void){return mounts[0].active?&mounts[0].fs:(rixfs_t*)0;}
int vfs_root(rix_vfs_path_t*out){if(!out)return -1;out->node=&root.node;out->path[0]='/';out->path[1]=0;return 0;}
int vfs_lookup(const char*path,rix_vfs_path_t*out){if(!path||!out)return -1;if(vfs_normalize_path(path,out->path,sizeof(out->path)))return -1;if(out->path[0]=='/'&&!out->path[1])return vfs_root(out);return lookup_rixfs_path(out->path,out);}
int vfs_lookup_from(const rix_vfs_path_t*base,const char*path,rix_vfs_path_t*out){if(!base||!path||!out)return -1;if(path[0]=='/')return vfs_lookup(path,out);char joined[RIX_VFS_PATH_MAX];size_t bl=0;while(base->path[bl]){if(bl+1>=sizeof(joined))return -1;joined[bl]=base->path[bl];bl++;}if(bl==0||joined[bl-1]!='/'){if(bl+1>=sizeof(joined))return -1;joined[bl++]='/';}size_t i=0;while(path[i]){if(bl+1>=sizeof(joined))return -1;joined[bl++]=path[i++];}joined[bl]=0;return vfs_lookup(joined,out);}
int vfs_open(uint64_t pid,const char*path,uint32_t flags,uint32_t mode,int*out_fd){
    size_t ps;if(pid_slot(pid,&ps)||!path||!out_fd)return -1;rix_vfs_path_t p;int r=vfs_lookup(path,&p);if(r&&!(flags&RIX_VFS_O_CREAT))return -2;
    if(r){char parent[RIX_VFS_PATH_MAX],name[RIX_VFS_NAME_MAX+1];if(split_parent(path,parent,sizeof(parent),name,sizeof(name)))return -3;rix_vfs_path_t pp;if(vfs_lookup(parent,&pp)||!pp.node||pp.node->type!=RIX_VFS_DIR)return -4;rixfs_t*fs=vfs_root_fs();if(!fs)return -5;uint64_t ino;if(rixfs_create(fs,pp.node->inode,name,mode,0,0,&ino))return -6;if(vfs_lookup(path,&p))return -7;}
    rix_vnode_t node=*p.node;if(node.type!=RIX_VFS_FILE&&node.type!=RIX_VFS_DIR)return -8;if((flags&(RIX_VFS_O_WRONLY|RIX_VFS_O_RDWR))&&node.type==RIX_VFS_DIR)return -9;
    int fd=-1;for(size_t i=0;i<RIX_VFS_FD_MAX;i++)if(!fds[ps][i].used){fd=(int)i;break;}if(fd<0)return -10;fds[ps][fd].used=1;fds[ps][fd].type=(uint8_t)node.type;fds[ps][fd].writable=(uint8_t)((flags&(RIX_VFS_O_WRONLY|RIX_VFS_O_RDWR))!=0);fds[ps][fd].append=(uint8_t)((flags&RIX_VFS_O_APPEND)!=0);fds[ps][fd].inode=node.inode;fds[ps][fd].offset=0;
    if(flags&RIX_VFS_O_TRUNC){if(!fds[ps][fd].writable||node.type!=RIX_VFS_FILE||rixfs_truncate(vfs_root_fs(),node.inode,0)){fds[ps][fd].used=0;return -11;}}*out_fd=fd;return 0;
}
int vfs_pipe(uint64_t pid,int *read_fd,int *write_fd){size_t ps;if(pid_slot(pid,&ps)||!read_fd||!write_fd)return -1;int r=-1,w=-1;for(int i=0;i<(int)RIX_VFS_FD_MAX;i++)if(!fds[ps][i].used){if(r<0)r=i;else{w=i;break;}}if(r<0||w<0)return -2;for(size_t i=0;i<VFS_PIPE_MAX;i++)if(!pipe_slots[i].used){pipe_init(&pipe_slots[i].pipe);pipe_slots[i].used=1u;pipe_slots[i].refs=2u;fds[ps][r]=(vfs_fd_t){1u,VFS_FD_PIPE_READ,0u,0u,0u,0u,&pipe_slots[i].pipe,0u};fds[ps][w]=(vfs_fd_t){1u,VFS_FD_PIPE_WRITE,1u,0u,0u,0u,&pipe_slots[i].pipe,1u};*read_fd=r;*write_fd=w;return 0;}return -3;}
int vfs_dup(uint64_t pid,int old_fd,int *new_fd){size_t ps;if(pid_slot(pid,&ps)||old_fd<0||old_fd>=RIX_VFS_FD_MAX||!new_fd||!fds[ps][old_fd].used)return -1;int fd=-1;for(int i=0;i<(int)RIX_VFS_FD_MAX;i++)if(!fds[ps][i].used){fd=i;break;}if(fd<0)return -2;fds[ps][fd]=fds[ps][old_fd];if(fds[ps][fd].type==VFS_FD_PIPE_READ||fds[ps][fd].type==VFS_FD_PIPE_WRITE){for(size_t i=0;i<VFS_PIPE_MAX;i++)if(pipe_slots[i].used&&(&pipe_slots[i].pipe==fds[ps][fd].pipe)){if(pipe_slots[i].refs==UINT8_MAX){fds[ps][fd].used=0;return -3;}pipe_slots[i].refs++;break;}}*new_fd=fd;return 0;}
int vfs_clone_fds(uint64_t parent_pid,uint64_t child_pid){size_t parent,child;if(pid_slot(parent_pid,&parent)||pid_slot(child_pid,&child)||parent_pid==child_pid)return -1;for(size_t i=0;i<RIX_VFS_FD_MAX;i++)if(fds[parent][i].used){if(fds[child][i].used)return -2;fds[child][i]=fds[parent][i];if(fds[child][i].type==VFS_FD_PIPE_READ||fds[child][i].type==VFS_FD_PIPE_WRITE){int found=0;for(size_t j=0;j<VFS_PIPE_MAX;j++)if(pipe_slots[j].used&&(&pipe_slots[j].pipe==fds[child][i].pipe)){if(pipe_slots[j].refs==UINT8_MAX)return -3;pipe_slots[j].refs++;found=1;break;}if(!found)return -4;}}return 0;}
int vfs_close(uint64_t pid,int fd){size_t ps;if(pid_slot(pid,&ps)||fd<0||fd>=RIX_VFS_FD_MAX||!fds[ps][fd].used)return -1;if(fds[ps][fd].type==VFS_FD_PIPE_READ||fds[ps][fd].type==VFS_FD_PIPE_WRITE){rix_pipe_t *pipe=fds[ps][fd].pipe;for(size_t i=0;i<VFS_PIPE_MAX;i++)if(pipe_slots[i].used&&(&pipe_slots[i].pipe==pipe)){if(fds[ps][fd].pipe_write)pipe_close_write(pipe);else pipe_close_read(pipe);if(pipe_slots[i].refs)pipe_slots[i].refs--;if(!pipe_slots[i].refs)pipe_slots[i].used=0;break;}}fds[ps][fd].used=0;return 0;}
int vfs_close_all(uint64_t pid){size_t ps;if(pid_slot(pid,&ps))return -1;for(int fd=0;fd<(int)RIX_VFS_FD_MAX;fd++)if(fds[ps][fd].used)(void)vfs_close(pid,fd);return 0;}
int vfs_read(uint64_t pid,int fd,void*buffer,size_t size,size_t*out_read){
    size_t ps;if(pid_slot(pid,&ps)||fd<0||fd>=RIX_VFS_FD_MAX||!fds[ps][fd].used||(!buffer&&size)||!out_read)return -1;if(fds[ps][fd].type==VFS_FD_PIPE_READ)return pipe_read(fds[ps][fd].pipe,buffer,size,out_read);if(fds[ps][fd].type!=RIX_VFS_FILE)return -2;rixfs_t*fs=vfs_root_fs();
if(!fs)return -3;
    rixfs_inode_disk_t in;if(rixfs_read_inode(fs,fds[ps][fd].inode,&in))return -4;
    if(fds[ps][fd].offset>=in.size){*out_read=0;return 0;}uint64_t remain=in.size-fds[ps][fd].offset;if((uint64_t)size>remain)size=(size_t)remain;if(rixfs_read(fs,in.inode,fds[ps][fd].offset,buffer,size))return -5;fds[ps][fd].offset+=size;*out_read=size;return 0;
}
int vfs_write(uint64_t pid,int fd,const void*buffer,size_t size,size_t*out_written){
    size_t ps;if(pid_slot(pid,&ps)||fd<0||fd>=RIX_VFS_FD_MAX||!fds[ps][fd].used||(!buffer&&size)||!out_written)return -1;if(fds[ps][fd].type==VFS_FD_PIPE_WRITE)return pipe_write(fds[ps][fd].pipe,buffer,size,out_written);if(!fds[ps][fd].writable||fds[ps][fd].type!=RIX_VFS_FILE)return -2;rixfs_t*fs=vfs_root_fs();
if(!fs)return -3;
    rixfs_inode_disk_t in;if(rixfs_read_inode(fs,fds[ps][fd].inode,&in))return -4;uint64_t off=fds[ps][fd].append?in.size:fds[ps][fd].offset;if(off>UINT64_MAX-(uint64_t)size)return -5;if(off+(uint64_t)size>in.size&&rixfs_truncate(fs,in.inode,off+(uint64_t)size))return -6;if(size&&rixfs_write(fs,in.inode,off,buffer,size))return -7;fds[ps][fd].offset=off+size;*out_written=size;return 0;
}
int vfs_readdir(uint64_t pid,int fd,uint64_t*offset,rix_vfs_dirent_t*out,char*name,size_t cap){size_t ps;if(pid_slot(pid,&ps)||fd<0||fd>=RIX_VFS_FD_MAX||!fds[ps][fd].used||!offset||!out||!name)return -1;if(fds[ps][fd].type!=RIX_VFS_DIR)return -2;rixfs_dirent_disk_t e;int r=rixfs_readdir(vfs_root_fs(),fds[ps][fd].inode,offset,&e,name,cap);if(r)return r;out->inode=e.inode;out->type=e.type;return 0;}
int vfs_stat(const char*path,rix_vnode_t*out){if(!out)return -1;rix_vfs_path_t p;if(vfs_lookup(path,&p))return -2;*out=*p.node;return 0;}
int vfs_mkdir(const char*path,uint32_t mode,uint32_t uid,uint32_t gid){char parent[RIX_VFS_PATH_MAX],name[RIX_VFS_NAME_MAX+1];if(split_parent(path,parent,sizeof(parent),name,sizeof(name)))return -1;rix_vfs_path_t p;if(vfs_lookup(parent,&p)||p.node->type!=RIX_VFS_DIR)return -2;uint64_t ino;return rixfs_mkdir(vfs_root_fs(),p.node->inode,name,mode,uid,gid,&ino);}
int vfs_unlink(const char*path){char parent[RIX_VFS_PATH_MAX],name[RIX_VFS_NAME_MAX+1];if(split_parent(path,parent,sizeof(parent),name,sizeof(name)))return -1;rix_vfs_path_t p;if(vfs_lookup(parent,&p)||p.node->type!=RIX_VFS_DIR)return -2;return rixfs_unlink(vfs_root_fs(),p.node->inode,name);}
