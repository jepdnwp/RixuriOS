#pragma once
#include <stddef.h>
#include <stdint.h>
#include "../storage/block.h"
#define RIXFS_MAGIC 0x5249584653465331ULL
#define RIXFS_VERSION 1u
#define RIXFS_BLOCK_SECTOR 0u
#define RIXFS_INODE_SIZE 128u
#define RIXFS_NAME_MAX 255u
#define RIXFS_DIRECT_EXTENTS 4u
#define RIXFS_SECTOR_MAX 4096u
#define RIXFS_IFMT 0xF000u
#define RIXFS_IFREG 0x8000u
#define RIXFS_IFDIR 0x4000u
#define RIXFS_IFLNK 0xA000u

typedef struct { uint64_t magic; uint32_t version; uint32_t header_size; uint32_t sector_size; uint32_t flags; uint64_t total_sectors; uint64_t inode_table_sector; uint64_t inode_count; uint64_t data_start_sector; uint64_t root_inode; uint64_t generation; uint64_t checksum; uint64_t bitmap_sector; uint64_t bitmap_sectors; uint64_t journal_sector; uint64_t journal_sectors; uint64_t free_hint; uint64_t journal_generation; uint8_t reserved[384]; } __attribute__((packed)) rixfs_superblock_t;
typedef struct { uint64_t inode; uint32_t mode; uint32_t uid; uint32_t gid; uint32_t flags; uint64_t size; uint64_t generation; uint64_t extent_start[RIXFS_DIRECT_EXTENTS]; uint64_t extent_length[RIXFS_DIRECT_EXTENTS]; uint8_t reserved[32]; } __attribute__((packed)) rixfs_inode_disk_t;
typedef struct { rix_block_device_t *device; rixfs_superblock_t super; uint8_t mounted; } rixfs_t;
int rixfs_mount(rix_block_device_t *device,rixfs_t *fs);
int rixfs_format(rix_block_device_t *device,uint64_t inode_count);
int rixfs_format_standard_tree(rix_block_device_t *device,uint64_t inode_count);
int rixfs_read_inode(rixfs_t *fs,uint64_t inode,rixfs_inode_disk_t *out);
int rixfs_write_inode(rixfs_t *fs,uint64_t inode,const rixfs_inode_disk_t *in);
int rixfs_read(rixfs_t *fs,uint64_t inode,uint64_t offset,void *buffer,size_t size);
int rixfs_write(rixfs_t *fs,uint64_t inode,uint64_t offset,const void *buffer,size_t size);
int rixfs_truncate(rixfs_t *fs,uint64_t inode,uint64_t new_size);
int rixfs_create(rixfs_t *fs,uint64_t dir_inode,const char *name,uint32_t mode,uint32_t uid,uint32_t gid,uint64_t *out_inode);
int rixfs_mkdir(rixfs_t *fs,uint64_t dir_inode,const char *name,uint32_t mode,uint32_t uid,uint32_t gid,uint64_t *out_inode);
int rixfs_unlink(rixfs_t *fs,uint64_t dir_inode,const char *name);
int rixfs_rmdir(rixfs_t *fs,uint64_t dir_inode,const char *name);
int rixfs_sync(rixfs_t *fs);
int rixfs_fsck(rix_block_device_t *device,uint64_t *checked_inodes,uint64_t *referenced_sectors);
void rixfs_unmount(rixfs_t *fs);
