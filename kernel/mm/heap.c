#include "heap.h"
#include "pmm.h"
#include <stdint.h>

#define HEAP_PAGE_SIZE 4096ULL
#define HEAP_MAX_ALIGNMENT 4096ULL
#define HEAP_MAX_ALLOCS 256u

typedef struct {
    uintptr_t page;
    uintptr_t ptr;
    size_t size;
    uint8_t active;
} heap_allocation_t;

static uint8_t *current_page;
static size_t current_offset;
static heap_allocation_t allocations[HEAP_MAX_ALLOCS];

static uintptr_t align_ptr(uintptr_t value, size_t alignment) {
    uintptr_t mask = (uintptr_t)alignment - 1U;
    if (value > UINTPTR_MAX - mask) return UINTPTR_MAX;
    return (value + mask) & ~mask;
}

static int page_has_live_allocations(uintptr_t page) {
    for (size_t i = 0; i < HEAP_MAX_ALLOCS; ++i)
        if (allocations[i].active && allocations[i].page == page) return 1;
    return 0;
}

static heap_allocation_t *find_free_record(void) {
    for (size_t i = 0; i < HEAP_MAX_ALLOCS; ++i)
        if (!allocations[i].active && allocations[i].ptr == 0) return &allocations[i];
    for (size_t i = 0; i < HEAP_MAX_ALLOCS; ++i)
        if (!allocations[i].active) return &allocations[i];
    return NULL;
}

void heap_init(void) {
    current_page = NULL;
    current_offset = 0;
    for (size_t i = 0; i < HEAP_MAX_ALLOCS; ++i) {
        allocations[i].page = 0;
        allocations[i].ptr = 0;
        allocations[i].size = 0;
        allocations[i].active = 0;
    }
}

void *kmalloc(size_t size, size_t alignment) {
    if (size == 0) return NULL;
    if (alignment == 0) alignment = sizeof(uintptr_t);
    if ((alignment & (alignment - 1U)) != 0 || alignment > HEAP_MAX_ALIGNMENT) return NULL;

    heap_allocation_t *record = find_free_record();
    if (!record) return NULL;

    uintptr_t base = (uintptr_t)current_page;
    uintptr_t aligned = align_ptr(base + current_offset, alignment);
    if (!current_page || aligned == UINTPTR_MAX ||
        aligned + size < aligned || aligned + size > base + HEAP_PAGE_SIZE) {
        uint64_t phys = pmm_alloc_page();
        if (!phys) return NULL;
        current_page = (uint8_t *)(uintptr_t)phys;
        current_offset = 0;
        base = (uintptr_t)current_page;
        aligned = align_ptr(base, alignment);
        if (aligned == UINTPTR_MAX || aligned + size < aligned ||
            aligned + size > base + HEAP_PAGE_SIZE) {
            pmm_free_page(phys);
            current_page = NULL;
            current_offset = 0;
            return NULL;
        }
    }

    record->page = (uintptr_t)current_page;
    record->ptr = aligned;
    record->size = size;
    record->active = 1;
    current_offset = (size_t)(aligned + size - (uintptr_t)current_page);
    return (void *)aligned;
}

void kfree(void *ptr) {
    if (!ptr) return;
    uintptr_t address = (uintptr_t)ptr;
    for (size_t i = 0; i < HEAP_MAX_ALLOCS; ++i) {
        heap_allocation_t *record = &allocations[i];
        if (!record->active || record->ptr != address) continue;
        uintptr_t page = record->page;
        record->active = 0;
        record->ptr = 0;
        record->size = 0;
        if (!page_has_live_allocations(page)) {
            pmm_free_page(page);
            if ((uintptr_t)current_page == page) {
                current_page = NULL;
                current_offset = 0;
            }
        }
        return;
    }
}
