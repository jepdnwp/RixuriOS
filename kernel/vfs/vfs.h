#pragma once
#include <stddef.h>
#include <stdint.h>

#define RIX_VFS_PATH_MAX 4096
#define RIX_VFS_NAME_MAX 255

typedef enum { RIX_VFS_DIR=1, RIX_VFS_FILE=2, RIX_VFS_SYMLINK=3, RIX_VFS_DEVICE=4 } rix_vfs_type_t;
typedef struct { uint64_t inode; rix_vfs_type_t type; uint32_t mode; uint32_t uid; uint32_t gid; uint64_t size; } rix_vnode_t;

typedef struct { rix_vnode_t *node; char path[RIX_VFS_PATH_MAX]; } rix_vfs_path_t;

int vfs_init(void);
int vfs_normalize_path(const char *input,char *output,size_t output_size);
int vfs_root(rix_vfs_path_t *out);
int vfs_lookup(const char *path,rix_vfs_path_t *out);
