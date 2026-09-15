#pragma once
#include <stddef.h>
#include <stdint.h>
#define RIX_STATFS_VERSION 1u
#define RIX_STATFS_TYPE_RIXFS 0x52495846u
typedef struct { uint32_t version; uint32_t struct_size; uint32_t fs_type; uint32_t block_size; uint64_t total_blocks; uint64_t free_blocks; uint64_t avail_blocks; uint64_t total_inodes; uint64_t free_inodes; uint32_t mount_id; uint32_t flags; } rix_statfs_t;
_Static_assert(sizeof(rix_statfs_t)==64,"statfs layout must stay 64 bytes");
int statfs(const char *path, rix_statfs_t *out);
