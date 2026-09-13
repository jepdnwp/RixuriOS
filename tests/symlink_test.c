#include "kernel/fs/rixfs.h"
#include "kernel/fs/rixfs_dir.h"
#include "kernel/storage/block.h"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Phase S2 host coverage (rixfs layer): symlink create/read/type on a
 * fake disk. Traversal depth-cap needs VFS+process state and is
 * QEMU-proven instead (documented, not faked here). */

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

int main(void) {
    rixfs_t fs;
    memset(&fake_disk_dev, 0, sizeof(fake_disk_dev));
    memcpy(fake_disk_dev.name, "fake1", 6);
    fake_disk_dev.sector_size = DISK_SECTOR_SIZE;
    fake_disk_dev.sector_count = DISK_SECTORS;
    fake_disk_dev.max_sectors = 128;
    memset(disk, 0, sizeof(disk));

    assert(rixfs_format(&fake_disk_dev, 16) == 0);
    memset(&fs, 0, sizeof(fs));
    assert(rixfs_mount(&fake_disk_dev, &fs) == 0);

    /* Create + resolve + raw target readback. */
    uint64_t ino = 0;
    assert(rixfs_symlink(&fs, 1, "link1", "/bin/echo", 0, 0, &ino) == 0);
    assert(ino != 0);
    uint64_t found = 0;
    uint8_t type = 0;
    assert(rixfs_lookup_name(&fs, 1, "link1", &found, &type) == 0);
    assert(found == ino && type == RIXFS_DIR_TYPE_SYMLINK);
    char target[32];
    memset(target, 0, sizeof(target));
    assert(rixfs_read(&fs, ino, 0, target, 9) == 0);
    assert(memcmp(target, "/bin/echo", 9) == 0);

    /* Dangling targets are allowed; doubles and bad inputs are not. */
    assert(rixfs_symlink(&fs, 1, "dangle", "/nope/nothing", 0, 0, &ino) == 0);
    assert(rixfs_symlink(&fs, 1, "link1", "/bin/echo", 0, 0, &ino) == -2);
    assert(rixfs_symlink(&fs, 1, "bad", "", 0, 0, &ino) == -1);
    assert(rixfs_symlink(&fs, 1, "a/b", "/bin/echo", 0, 0, &ino) == -1);
    {
        static char big[RIXFS_SYMLINK_TARGET_MAX + 8];
        memset(big, 'x', sizeof(big) - 1);
        big[sizeof(big) - 1] = 0;
        assert(rixfs_symlink(&fs, 1, "toolong", big, 0, 0, &ino) == -1);
    }

    /* Unlink removes the link; lookup then misses. */
    assert(rixfs_unlink(&fs, 1, "link1") == 0);
    assert(rixfs_lookup_name(&fs, 1, "link1", &found, &type) != 0);

    /* Phase F1c host coverage: compact dirents. A dozen entries must
     * share a single sector (no extent growth), and remove/rename on
     * packed entries must not disturb siblings. */
    uint64_t sub = 0;
    assert(rixfs_mkdir(&fs, 1, "compact", 0755, 0, 0, &sub) == 0);
    char nm[16];
    for (int i = 0; i < 12; ++i) {
        snprintf(nm, sizeof(nm), "f%02d", i);
        uint64_t c = 0;
        assert(rixfs_create(&fs, sub, nm, 0644, 0, 0, &c) == 0);
        assert(c != 0);
    }
    rixfs_inode_disk_t di;
    assert(rixfs_read_inode(&fs, sub, &di) == 0);
    assert(di.size == DISK_SECTOR_SIZE);
    assert(di.extent_length[0] == 1 && di.extent_length[1] == 0 &&
           di.extent_length[2] == 0 && di.extent_length[3] == 0);
    for (int i = 0; i < 12; ++i) {
        snprintf(nm, sizeof(nm), "f%02d", i);
        assert(rixfs_lookup_name(&fs, sub, nm, &found, &type) == 0);
        assert(type == RIXFS_DIR_TYPE_FILE);
    }
    assert(rixfs_unlink(&fs, sub, "f03") == 0);
    assert(rixfs_lookup_name(&fs, sub, "f03", &found, &type) != 0);
    {
        uint64_t c = 0;
        assert(rixfs_create(&fs, sub, "newf", 0644, 0, 0, &c) == 0);
    }
    assert(rixfs_read_inode(&fs, sub, &di) == 0);
    assert(di.size == DISK_SECTOR_SIZE);
    assert(rixfs_rename(&fs, sub, "f01", sub, "g01", 0) == 0);
    assert(rixfs_lookup_name(&fs, sub, "f01", &found, &type) != 0);
    assert(rixfs_lookup_name(&fs, sub, "g01", &found, &type) == 0);
    /* Replace-rename onto a packed entry must not clobber siblings. */
    assert(rixfs_rename(&fs, sub, "f02", sub, "g01", 1) == 0);
    assert(rixfs_lookup_name(&fs, sub, "g01", &found, &type) == 0);
    assert(rixfs_lookup_name(&fs, sub, "f00", &found, &type) == 0);
    assert(rixfs_lookup_name(&fs, sub, "f04", &found, &type) == 0);
    uint64_t sub2 = 0;
    assert(rixfs_mkdir(&fs, 1, "compact2", 0755, 0, 0, &sub2) == 0);
    assert(rixfs_rename(&fs, sub, "f04", sub2, "h04", 0) == 0);
    assert(rixfs_lookup_name(&fs, sub, "f04", &found, &type) != 0);
    assert(rixfs_lookup_name(&fs, sub2, "h04", &found, &type) == 0);
    {
        uint64_t off = 0;
        rixfs_dirent_disk_t e;
        char name[64];
        int n = 0;
        for (;;) {
            int r = rixfs_readdir(&fs, sub, &off, &e, name, sizeof(name));
            if (r == 1) break;
            assert(r == 0);
            ++n;
        }
        assert(n == 10);
    }
    rixfs_unmount(&fs);

    /* Packed layout must pass fsck. */
    {
        uint64_t checked = 0, refs = 0;
        assert(rixfs_fsck(&fake_disk_dev, &checked, &refs) == 0);
        assert(checked > 0 && refs > 0);
    }
    return 0;
}
