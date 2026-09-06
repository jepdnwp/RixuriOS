#pragma once
#include <stddef.h>
#include <stdint.h>
#include "rixfs.h"

#define RIXFS_DIR_MAGIC 0x52444952u
#define RIXFS_DIR_TYPE_UNKNOWN 0u
#define RIXFS_DIR_TYPE_DIR 1u
#define RIXFS_DIR_TYPE_FILE 2u
#define RIXFS_DIR_TYPE_SYMLINK 3u
#define RIXFS_DIRENT_MIN_SIZE 16u

typedef struct {
    uint64_t inode;
    uint16_t record_size;
    uint8_t type;
    uint8_t name_length;
    uint32_t flags;
} __attribute__((packed)) rixfs_dirent_disk_t;

int rixfs_lookup_name(rixfs_t *fs, uint64_t dir_inode, const char *name, uint64_t *out_inode, uint8_t *out_type);
int rixfs_readdir(rixfs_t *fs, uint64_t dir_inode, uint64_t *offset, rixfs_dirent_disk_t *out, char *name, size_t name_capacity);
