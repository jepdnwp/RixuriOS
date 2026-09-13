#include "../kernel/mm/pmm.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>

static void put32(uint8_t *entry, size_t offset, uint32_t value) {
    memcpy(entry + offset, &value, sizeof(value));
}

static void put64(uint8_t *entry, size_t offset, uint64_t value) {
    memcpy(entry + offset, &value, sizeof(value));
}

/* lockdep warn path needs a sink; warnings fail the test instead. */
static unsigned lockdep_warnings;
void serial_write(const char *s) { (void)s; ++lockdep_warnings; }

int main(void) {
    uint8_t memory_map[40];
    memset(memory_map, 0, sizeof(memory_map));
    put32(memory_map, 0u, 7u);       /* EFI_CONVENTIONAL_MEMORY */
    put64(memory_map, 8u, 0u);       /* base */
    put64(memory_map, 24u, 512u);    /* two MiB of pages */

    pmm_init(memory_map, sizeof(memory_map), sizeof(memory_map),
             0x100000u, 0x101000u, 0x102000u, 0x1000u);

    assert(pmm_is_managed(0x100000u));
    assert(pmm_is_reserved(0x100000u));
    assert(pmm_is_in_use(0x100000u));
    assert(pmm_is_reserved(0x102000u));
    assert(pmm_is_in_use(0x102000u));

    uint64_t free_before = pmm_free_pages();
    pmm_free_page(0x100000u);
    pmm_free_page(0x102000u);
    assert(pmm_is_in_use(0x100000u));
    assert(pmm_is_in_use(0x102000u));
    assert(pmm_free_pages() == free_before);

    uint64_t page = pmm_alloc_page_below(0x104000u);
    assert(page == 0x101000u || page == 0x103000u);
    assert(!pmm_is_reserved(page));
    assert(pmm_is_in_use(page));
    pmm_free_page(page);
    assert(pmm_free_pages() == free_before);
    /* Double free is a harmless no-op; counts stay stable. */
    pmm_free_page(page);
    assert(pmm_free_pages() == free_before);

    /* Contiguous multi-page alloc, range free, and edge rejections. */
    uint64_t base = pmm_alloc_pages(4u);
    assert(base != 0u && (base & 0xFFFu) == 0u);
    for (size_t i = 0; i < 4u; ++i)
        assert(pmm_is_in_use(base + i * 0x1000u));
    assert(pmm_free_pages() == free_before - 4u);
    pmm_free_page_range(base, 4u);
    assert(pmm_free_pages() == free_before);
    assert(pmm_alloc_pages(0u) == 0u);
    assert(pmm_alloc_pages(1u << 30) == 0u);
    assert(pmm_alloc_page_below(0x1000u) == 0u);
    assert(pmm_free_pages() == free_before);
    assert(lockdep_warnings == 0u);
    return 0;
}
