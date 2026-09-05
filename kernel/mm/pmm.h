#pragma once
#include <stdint.h>
#include <stddef.h>

#define RIXURI_PAGE_SIZE 4096ULL

void pmm_init(const void *memory_map, uint64_t memory_map_size, uint64_t descriptor_size,
             uint64_t kernel_base, uint64_t kernel_end);
uint64_t pmm_alloc_page(void);
void pmm_free_page(uint64_t physical_address);
uint64_t pmm_total_pages(void);
uint64_t pmm_free_pages(void);
