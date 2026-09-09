#include "kernel/fs/rixfs.h"
#include "kernel/storage/block.h"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define DISK_SECTORS 128u
#define DISK_SECTOR_SIZE 512u

static uint8_t disk[DISK_SECTORS][DISK_SECTOR_SIZE];
static rix_block_device_t fake_disk_dev;

uint64_t pmm_alloc_page(void) {
    void *p = aligned_alloc(4096, 4096);
    assert(p != 0);
    memset(p, 0, 4096);
    return (uint64_t)(uintptr_t)p;
}

void pmm_free_page(uint64_t physical_address) {
    free((void *)(uintptr_t)physical_address);
}

int block_submit(rix_block_device_t *device, rix_bio_t *bio) {
    uint8_t *base;
    if (!device || !bio || device != &fake_disk_dev) return -1;
    if (bio->op == RIX_BIO_FLUSH) {
        bio->state = RIX_BIO_COMPLETE;
        bio->error = 0;
        return 0;
    }
    if (bio->sector + bio->count > DISK_SECTORS || !bio->buffer ||
        bio->buffer_size < (size_t)bio->count * DISK_SECTOR_SIZE)
        return -1;
    base = &disk[bio->sector][0];
    if (bio->op == RIX_BIO_READ)
        memcpy(bio->buffer, base, (size_t)bio->count * DISK_SECTOR_SIZE);
    else if (bio->op == RIX_BIO_WRITE)
        memcpy(base, bio->buffer, (size_t)bio->count * DISK_SECTOR_SIZE);
    else
        return -1;
    bio->state = RIX_BIO_COMPLETE;
    bio->error = 0;
    return 0;
}

static uint64_t fnv1a(const void *data, size_t n) {
    const uint8_t *p = (const uint8_t *)data;
    uint64_t h = 1469598103934665603ULL;
    while (n--) h = (h ^ (uint64_t)*p++) * 1099511628211ULL;
    return h;
}

static void make_device(void) {
    memset(&fake_disk_dev, 0, sizeof(fake_disk_dev));
    memcpy(fake_disk_dev.name, "fake0", 6);
    fake_disk_dev.sector_size = DISK_SECTOR_SIZE;
    fake_disk_dev.sector_count = DISK_SECTORS;
    fake_disk_dev.max_sectors = 128;
}

/* Geometry satisfying every rixfs_mount consistency check. */
static void write_superblock(int corrupt) {
    rixfs_superblock_t sb;
    memset(&sb, 0, sizeof(sb));
    sb.magic = RIXFS_MAGIC;
    sb.version = RIXFS_VERSION;
    sb.header_size = (uint32_t)sizeof(sb);
    sb.sector_size = DISK_SECTOR_SIZE;
    sb.total_sectors = DISK_SECTORS;
    sb.inode_table_sector = 1;
    sb.inode_count = 8;
    sb.bitmap_sector = 3;
    sb.bitmap_sectors = 1;
    sb.journal_sector = 4;
    sb.journal_sectors = 2;
    sb.data_start_sector = 8;
    sb.root_inode = 1;
    sb.generation = 1;
    sb.checksum = 0;
    sb.checksum = fnv1a(&sb, sizeof(sb));
    if (corrupt) sb.checksum ^= 0xffu;
    memcpy(disk[0], &sb, sizeof(sb) < DISK_SECTOR_SIZE ? sizeof(sb) : DISK_SECTOR_SIZE);
}

int main(void) {
    rixfs_t fs;
    make_device();
    memset(disk, 0, sizeof(disk));

    /* Zeroed disk (like a foreign NTFS volume): superblock mismatch. */
    memset(&fs, 0, sizeof(fs));
    assert(rixfs_mount(&fake_disk_dev, &fs) == -5);

    /* Well-formed superblock mounts. */
    write_superblock(0);
    memset(&fs, 0, sizeof(fs));
    assert(rixfs_mount(&fake_disk_dev, &fs) == 0);
    assert(fs.mounted && fs.super.root_inode == 1);
    assert(fs.super.total_sectors == DISK_SECTORS);
    rixfs_unmount(&fs);
    assert(!fs.mounted);

    /* Single-bit superblock corruption is rejected the same way. */
    write_superblock(1);
    memset(&fs, 0, sizeof(fs));
    assert(rixfs_mount(&fake_disk_dev, &fs) == -5);

    /* Degenerate device geometry is rejected before any I/O. */
    fake_disk_dev.sector_count = 0;
    memset(&fs, 0, sizeof(fs));
    assert(rixfs_mount(&fake_disk_dev, &fs) == -1);
    fake_disk_dev.sector_count = DISK_SECTORS;
    fake_disk_dev.sector_size = 0;
    assert(rixfs_mount(&fake_disk_dev, &fs) == -1);
    return 0;
}
