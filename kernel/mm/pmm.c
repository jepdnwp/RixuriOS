#include "pmm.h"
#include <stddef.h>

/* Early allocator deliberately uses a fixed bitmap. The architectural ceiling is
 * 128 GiB physical RAM: 128 GiB / 4 KiB pages = 33,554,432 bits = 4 MiB. */
#define PMM_MAX_PHYS (128ULL * 1024ULL * 1024ULL * 1024ULL)
#define PMM_MAX_PAGES (PMM_MAX_PHYS / RIXURI_PAGE_SIZE)
#define PMM_BITMAP_WORDS (PMM_MAX_PAGES / 64ULL)

static uint64_t page_bitmap[PMM_BITMAP_WORDS];
static uint64_t total_pages_count;
static uint64_t free_pages_count;

/* UEFI memory types that can be reclaimed after ExitBootServices(). */
static int usable_type(uint32_t type) {
    return type == 1 || type == 2 || type == 3 || type == 4 || type == 7;
}

static void mark_range(uint64_t base, uint64_t pages, int freeable) {
    uint64_t first = (base + RIXURI_PAGE_SIZE - 1) / RIXURI_PAGE_SIZE;
    uint64_t last = first + pages;
    if (first >= PMM_MAX_PAGES) return;
    if (last > PMM_MAX_PAGES) last = PMM_MAX_PAGES;
    for (uint64_t p = first; p < last; ++p) {
        uint64_t *word = &page_bitmap[p >> 6];
        uint64_t bit = 1ULL << (p & 63);
        if (freeable) {
            if (*word & bit) {
                *word &= ~bit;
                ++free_pages_count;
            }
        } else {
            if (!(*word & bit)) {
                *word |= bit;
                if (free_pages_count) --free_pages_count;
            }
        }
    }
}

void pmm_init(const void *memory_map, uint64_t memory_map_size,
             uint64_t descriptor_size, uint64_t kernel_base,
             uint64_t kernel_end) {
    for (size_t i = 0; i < PMM_BITMAP_WORDS; ++i) page_bitmap[i] = UINT64_MAX;
    total_pages_count = 0;
    free_pages_count = 0;

    if (!memory_map || descriptor_size < 40 || descriptor_size > 4096) return;

    for (uint64_t off = 0; off + descriptor_size <= memory_map_size; off += descriptor_size) {
        const unsigned char *d = (const unsigned char *)memory_map + off;
        uint32_t type = *(const uint32_t *)(d + 0);
        uint64_t base = *(const uint64_t *)(d + 8);
        uint64_t pages = *(const uint64_t *)(d + 24);
        if (!usable_type(type) || base >= PMM_MAX_PHYS || pages == 0) continue;
        if (base + pages * RIXURI_PAGE_SIZE > PMM_MAX_PHYS)
            pages = (PMM_MAX_PHYS - base) / RIXURI_PAGE_SIZE;
        total_pages_count += pages;
        mark_range(base, pages, 1);
    }

    /* Keep low memory and the loaded kernel unavailable to early allocations. */
    mark_range(0, 0x100000 / RIXURI_PAGE_SIZE, 0);
    if (kernel_end > kernel_base)
        mark_range(kernel_base, (kernel_end - kernel_base + RIXURI_PAGE_SIZE - 1) / RIXURI_PAGE_SIZE, 0);
}

uint64_t pmm_alloc_page(void) {
    for (uint64_t w = 0; w < PMM_BITMAP_WORDS; ++w) {
        uint64_t bits = page_bitmap[w];
        if (bits == UINT64_MAX) continue;
        uint64_t inv = ~bits;
        unsigned bit = (unsigned)__builtin_ctzll(inv);
        page_bitmap[w] |= 1ULL << bit;
        if (free_pages_count) --free_pages_count;
        return ((w * 64ULL) + bit) * RIXURI_PAGE_SIZE;
    }
    return 0;
}

void pmm_free_page(uint64_t physical_address) {
    if ((physical_address & (RIXURI_PAGE_SIZE - 1)) != 0) return;
    uint64_t page = physical_address / RIXURI_PAGE_SIZE;
    if (page >= PMM_MAX_PAGES) return;
    uint64_t *word = &page_bitmap[page >> 6];
    uint64_t bit = 1ULL << (page & 63);
    if (*word & bit) return;
    *word |= bit;
    ++free_pages_count;
}

uint64_t pmm_total_pages(void) { return total_pages_count; }
uint64_t pmm_free_pages(void) { return free_pages_count; }
