#pragma once

#include <stdint.h>
#include <stddef.h>

#define RIXURI_PAGE_SIZE 4096ULL
#define RIXURI_MAX_PHYS_BYTES (128ULL * 1024ULL * 1024ULL * 1024ULL)
#define RIXURI_MAX_PAGES (RIXURI_MAX_PHYS_BYTES / RIXURI_PAGE_SIZE)
#define RIXURI_BITMAP_WORDS ((RIXURI_MAX_PAGES + 63ULL) / 64ULL)

void pmm_init(const void *memory_map, uint64_t memory_map_size,
             uint64_t descriptor_size, uint64_t kernel_base,
             uint64_t kernel_end, uint64_t boot_info,
             uint64_t boot_info_size);
uint64_t pmm_alloc_page(void);
uint64_t pmm_alloc_page_below(uint64_t max_physical_exclusive);
void pmm_reserve_page(uint64_t physical_address);
void pmm_free_page(uint64_t physical_address);
uint64_t pmm_total_pages(void);
uint64_t pmm_free_pages(void);
