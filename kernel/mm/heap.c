#include "heap.h"
#include "pmm.h"
#include <stdint.h>

#define HEAP_PAGE_SIZE 4096ULL
#define HEAP_MAX_ALIGNMENT 4096ULL

static uint8_t *current_page;
static size_t current_offset;

static uintptr_t align_ptr(uintptr_t value, size_t alignment) {
    uintptr_t mask = (uintptr_t)alignment - 1U;
    return (value + mask) & ~mask;
}

void heap_init(void) {
    current_page = NULL;
    current_offset = 0;
}

void *kmalloc(size_t size, size_t alignment) {
    if (size == 0) return NULL;
    if (alignment == 0) alignment = sizeof(uintptr_t);
    if ((alignment & (alignment - 1U)) != 0 || alignment > HEAP_MAX_ALIGNMENT) return NULL;

    uintptr_t base = (uintptr_t)current_page;
    uintptr_t aligned = align_ptr(base + current_offset, alignment);
    if (!current_page || aligned < base || aligned + size < aligned || aligned + size > base + HEAP_PAGE_SIZE) {
        uint64_t phys = pmm_alloc_page();
        if (!phys) return NULL;
        current_page = (uint8_t *)(uintptr_t)phys;
        current_offset = 0;
        base = (uintptr_t)current_page;
        aligned = align_ptr(base, alignment);
        if (aligned < base || aligned + size < aligned || aligned + size > base + HEAP_PAGE_SIZE) return NULL;
    }
    current_offset = (size_t)(aligned + size - (uintptr_t)current_page);
    return (void *)aligned;
}

void kfree(void *ptr) {
    /* Early heap allocations are intentionally monotonic. A real reclaiming
       slab/size-class allocator will replace this in the general heap phase. */
    (void)ptr;
}
