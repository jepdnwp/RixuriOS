#include "pmm.h"
#include <stddef.h>
#include <stdint.h>

#define PMM_MAX_PHYS RIXURI_MAX_PHYS_BYTES
#define PMM_MAX_PAGES RIXURI_MAX_PAGES
#define EFI_DESCRIPTOR_MIN_SIZE 40ULL

static uint64_t page_bitmap[RIXURI_BITMAP_WORDS];
static uint64_t total_pages_count;
static uint64_t free_pages_count;

static int usable_type(uint32_t type) {
    return type == 1 || type == 2 || type == 3 || type == 4 || type == 7;
}

static uint64_t align_up_page(uint64_t value) {
    if (value > UINT64_MAX - (RIXURI_PAGE_SIZE - 1ULL)) return UINT64_MAX;
    return (value + RIXURI_PAGE_SIZE - 1ULL) & ~(RIXURI_PAGE_SIZE - 1ULL);
}

static void mark_range(uint64_t base, uint64_t pages, int freeable) {
    if (pages == 0 || base >= PMM_MAX_PHYS) return;
    uint64_t first_addr = align_up_page(base);
    if (first_addr == UINT64_MAX || first_addr >= PMM_MAX_PHYS) return;
    uint64_t first = first_addr / RIXURI_PAGE_SIZE;
    uint64_t max_pages = (PMM_MAX_PHYS - first_addr) / RIXURI_PAGE_SIZE;
    if (pages > max_pages) pages = max_pages;
    uint64_t last = first + pages;

    for (uint64_t p = first; p < last; ++p) {
        uint64_t *word = &page_bitmap[p >> 6];
        uint64_t bit = 1ULL << (p & 63ULL);
        if (freeable) {
            if (*word & bit) {
                *word &= ~bit;
                ++free_pages_count;
            }
        } else if (!(*word & bit)) {
            *word |= bit;
            if (free_pages_count) --free_pages_count;
        }
    }
}

void pmm_init(const void *memory_map, uint64_t memory_map_size,
             uint64_t descriptor_size, uint64_t kernel_base,
             uint64_t kernel_end, uint64_t boot_info,
             uint64_t boot_info_size) {
    for (size_t i = 0; i < RIXURI_BITMAP_WORDS; ++i) page_bitmap[i] = UINT64_MAX;
    total_pages_count = 0;
    free_pages_count = 0;

    if (!memory_map || descriptor_size < EFI_DESCRIPTOR_MIN_SIZE ||
        descriptor_size > 4096 || memory_map_size < descriptor_size) return;

    uint64_t offset = 0;
    while (offset <= memory_map_size - descriptor_size) {
        const unsigned char *d = (const unsigned char *)memory_map + offset;
        uint32_t type;
        uint64_t base, pages;
        __builtin_memcpy(&type, d + 0, sizeof(type));
        __builtin_memcpy(&base, d + 8, sizeof(base));
        __builtin_memcpy(&pages, d + 24, sizeof(pages));

        if (usable_type(type) && pages && base < PMM_MAX_PHYS) {
            uint64_t max_pages = (PMM_MAX_PHYS - base) / RIXURI_PAGE_SIZE;
            if (pages > max_pages) pages = max_pages;
            total_pages_count += pages;
            mark_range(base, pages, 1);
        }
        offset += descriptor_size;
    }

    /* Low firmware/legacy region is deliberately unavailable. */
    mark_range(0, 0x100000ULL / RIXURI_PAGE_SIZE, 0);

    if (kernel_end > kernel_base) {
        uint64_t size = kernel_end - kernel_base;
        uint64_t pages = (size + RIXURI_PAGE_SIZE - 1ULL) / RIXURI_PAGE_SIZE;
        mark_range(kernel_base, pages, 0);
    }

    /* Boot information and the final memory-map buffer remain live after
       ExitBootServices and must not be returned to the allocator. */
    if (boot_info_size) {
        uint64_t pages = (boot_info_size + RIXURI_PAGE_SIZE - 1ULL) / RIXURI_PAGE_SIZE;
        mark_range(boot_info, pages, 0);
    }
    mark_range((uint64_t)(uintptr_t)memory_map,
               (memory_map_size + RIXURI_PAGE_SIZE - 1ULL) / RIXURI_PAGE_SIZE, 0);
}

uint64_t pmm_alloc_page(void) {
    for (uint64_t w = 0; w < RIXURI_BITMAP_WORDS; ++w) {
        uint64_t bits = page_bitmap[w];
        if (bits == UINT64_MAX) continue;
        uint64_t inv = ~bits;
        unsigned bit = (unsigned)__builtin_ctzll(inv);
        uint64_t page = w * 64ULL + bit;
        if (page >= PMM_MAX_PAGES) return 0;
        page_bitmap[w] |= 1ULL << bit;
        if (free_pages_count) --free_pages_count;
        return page * RIXURI_PAGE_SIZE;
    }
    return 0;
}

void pmm_free_page(uint64_t physical_address) {
    if ((physical_address & (RIXURI_PAGE_SIZE - 1ULL)) != 0) return;
    uint64_t page = physical_address / RIXURI_PAGE_SIZE;
    if (page >= PMM_MAX_PAGES) return;
    uint64_t *word = &page_bitmap[page >> 6];
    uint64_t bit = 1ULL << (page & 63ULL);
    if (*word & bit) return;
    *word |= bit;
    ++free_pages_count;
}

uint64_t pmm_total_pages(void) { return total_pages_count; }
uint64_t pmm_free_pages(void) { return free_pages_count; }
