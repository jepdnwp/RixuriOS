#include "../kernel/fs/rixfs.h"
#include "../kernel/storage/block.h"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DISK_SECTORS 128u
#define DISK_SECTOR_SIZE 512u

static uint8_t disk[DISK_SECTORS][DISK_SECTOR_SIZE];
static rix_block_device_t fake_dev;

uint64_t pmm_alloc_page(void) {
    void *p = aligned_alloc(4096, 4096);
    assert(p != 0);
    memset(p, 0, 4096);
    return (uint64_t)(uintptr_t)p;
}
void pmm_free_page(uint64_t pa) { free((void *)(uintptr_t)pa); }

int block_submit(rix_block_device_t *d, rix_bio_t *bio) {
    if (!d || !bio || d != &fake_dev) return -1;
    if (bio->op == RIX_BIO_FLUSH) { bio->state = RIX_BIO_COMPLETE; bio->error = 0; return 0; }
    if (bio->sector + bio->count > DISK_SECTORS || !bio->buffer ||
        bio->buffer_size < (size_t)bio->count * DISK_SECTOR_SIZE) return -1;
    uint8_t *base = &disk[bio->sector][0];
    if (bio->op == RIX_BIO_READ) memcpy(bio->buffer, base, (size_t)bio->count * DISK_SECTOR_SIZE);
    else if (bio->op == RIX_BIO_WRITE) memcpy(base, bio->buffer, (size_t)bio->count * DISK_SECTOR_SIZE);
    else return -1;
    bio->state = RIX_BIO_COMPLETE; bio->error = 0; return 0;
}

int main(void) {
    memset(&fake_dev, 0, sizeof(fake_dev));
    memcpy(fake_dev.name, "fake-statfs", 12);
    fake_dev.sector_size = DISK_SECTOR_SIZE;
    fake_dev.sector_count = DISK_SECTORS;
    fake_dev.max_sectors = 128;
    memset(disk, 0, sizeof(disk));

    assert(rixfs_format(&fake_dev, 16) == 0);
    rixfs_t fs;
    memset(&fs, 0, sizeof(fs));
    assert(rixfs_mount(&fake_dev, &fs) == 0);

    uint64_t total = 0, bfree = 0, free_ino = 0;
    assert(rixfs_statfs(&fs, &total, &bfree, &free_ino) == 0);
    assert(total == DISK_SECTORS);
    assert(bfree > 0 && bfree < total);
    assert(free_ino == 15u);
    printf("base total=%llu free=%llu free_ino=%llu PASS\n",
           (unsigned long long)total, (unsigned long long)bfree,
           (unsigned long long)free_ino);

    uint64_t free_before = bfree, ino_before = free_ino;
    uint64_t ino = 0;
    assert(rixfs_create(&fs, 1, "f1", 0644, 0, 0, &ino) == 0);
    assert(rixfs_statfs(&fs, &total, &bfree, &free_ino) == 0);
    assert(free_ino + 1 == ino_before);
    printf("create ino-free PASS\n");

    /* One sector write must consume free blocks (grow then write). */
    uint8_t w[DISK_SECTOR_SIZE];
    memset(w, 0xA5, sizeof(w));
    assert(rixfs_truncate(&fs, ino, sizeof(w)) == 0);
    assert(rixfs_write(&fs, ino, 0, w, sizeof(w)) == 0);
    assert(rixfs_statfs(&fs, &total, &bfree, &free_ino) == 0);
    assert(bfree + 1 == free_before || bfree < free_before);
    printf("write consumes PASS (free %llu -> %llu)\n",
           (unsigned long long)free_before, (unsigned long long)bfree);

    /* Null / unmounted fail closed. */
    assert(rixfs_statfs(NULL, &total, &bfree, &free_ino) != 0);
    rixfs_t bad;
    memset(&bad, 0, sizeof(bad));
    assert(rixfs_statfs(&bad, &total, &bfree, &free_ino) != 0);
    printf("negative PASS\n");

    rixfs_unmount(&fs);
    printf("statfs tests: PASS\n");
    return 0;
}
