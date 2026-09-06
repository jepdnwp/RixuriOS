#pragma once
#include <stddef.h>
#include <stdint.h>
#include "../storage/block.h"
#include "../fs/rixfs_dir.h"
#define RIX_VFS_PATH_MAX 4096
#define RIX_VFS_NAME_MAX 255
#define RIX_VFS_FD_MAX 64
#define RIX_VFS_O_WRONLY 1u
#define RIX_VFS_O_RDWR 2u
#define RIX_VFS_O_CREAT 4u
#define RIX_VFS_O_TRUNC 8u
typedef enum { RIX_VFS_DIR=1, RIX_VFS_FILE=2, RIX_VFS_SYMLINK=3, RIX_VFS_DEVICE=4 } rix_vfs_type_t;
typedef struct { uint64_t inode; rix_vfs_type_t type; uint32_t mode; uint32_t uid; uint32_t gid; uint64_t size; } rix_vnode_t;
typedef struct { rix_vnode_t *node; char path[RIX_VFS_PATH_MAX]; } rix_vfs_path_t;
typedef struct { uint64_t inode; uint8_t type; uint8_t reserved[7]; } rix_vfs_dirent_t;
int vfs_init(void);
int vfs_mount_root(rix_block_device_t *device);
int vfs_unmount_root(void);
rixfs_t *vfs_root_fs(void);
int vfs_normalize_path(const char *input,char *output,size_t output_size);
int vfs_root(rix_vfs_path_t *out);
int vfs_lookup(const char *path,rix_vfs_path_t *out);
int vfs_lookup_from(const rix_vfs_path_t *base,const char *path,rix_vfs_path_t *out);
int vfs_open(uint64_t pid,const char *path,uint32_t flags,uint32_t mode,int *out_fd);
int vfs_close(uint64_t pid,int fd);
int vfs_read(uint64_t pid,int fd,void *buffer,size_t size,size_t *out_read);
int vfs_write(uint64_t pid,int fd,const void *buffer,size_t size,size_t *out_written);
int vfs_readdir(uint64_t pid,int fd,uint64_t *offset,rix_vfs_dirent_t *out,char *name,size_t name_capacity);
int vfs_stat(const char *path,rix_vnode_t *out);
int vfs_mkdir(const char *path,uint32_t mode,uint32_t uid,uint32_t gid);
int vfs_unlink(const char *path);
