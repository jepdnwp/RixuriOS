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
uint64_t pmm_alloc_pages(size_t count);
void pmm_free_page_range(uint64_t physical_address, size_t count);
uint64_t pmm_alloc_page_below(uint64_t max_physical_exclusive);
void pmm_reserve_page(uint64_t physical_address);
void pmm_free_page(uint64_t physical_address);
uint64_t pmm_total_pages(void);
uint64_t pmm_free_pages(void);
/* CR3-switch diagnostics: query PMM ownership of a 4 KiB-aligned page. */
int pmm_is_managed(uint64_t physical_address);
int pmm_is_in_use(uint64_t physical_address);
int pmm_is_reserved(uint64_t physical_address);
/* Describe the UEFI memory-map region containing pa (0=found, 1=not found,
 * -1=no map saved). end is exclusive. usable follows the allocator's own
 * usable_type() so the report cannot disagree with PMM behavior. */
int pmm_region_info(uint64_t physical_address,uint64_t *out_base,uint64_t *out_end,uint32_t *out_type,int *out_usable);
