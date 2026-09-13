#include "heap.h"
#include "pmm.h"
#include "../sync/lock.h"
#include "../sync/lockdep.h"
#include <stdint.h>

#define HEAP_PAGE_SIZE 4096ULL
#define HEAP_MAX_ALIGNMENT 4096ULL
#define HEAP_MAX_ALLOCS 256u
/* Minimum tail worth tracking as its own free block; smaller slack is
 * absorbed into the allocation (bounded waste, no record pressure). */
#define HEAP_MIN_SPLIT 16u

/* Record states: live (active), free block (!active, ptr != 0: memory
 * is free but still owned by this page), never-used (!active, ptr==0).
 * kfree keeps ptr/size so the block is reusable; records are dropped
 * only when their whole page returns to the PMM.
 *
 * Locking: heap_lock (irqsave) guards all state. kfree/kmalloc call
 * into the PMM, giving the order heap -> pmm (never reversed). */

typedef struct {
    uintptr_t page;
    uintptr_t ptr;
    size_t size;
    uint8_t active;
} heap_allocation_t;

static uint8_t *current_page;
static size_t current_offset;
static heap_allocation_t allocations[HEAP_MAX_ALLOCS];
static rix_spinlock_t heap_lock;
/* lockdep rank 20: nests inside PMM (30), never inside vmm-map. */
static unsigned heap_lockdep_class;

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

static heap_allocation_t *find_empty_record(void) {
    for (size_t i = 0; i < HEAP_MAX_ALLOCS; ++i)
        if (!allocations[i].active && allocations[i].ptr == 0) return &allocations[i];
    return NULL;
}

/* First-fit free block whose start already satisfies the alignment
 * (front waste is never tracked, so it must be zero) and whose span
 * covers the request. */
static heap_allocation_t *find_free_block(size_t size, size_t alignment, uintptr_t *out_ptr) {
    for (size_t i = 0; i < HEAP_MAX_ALLOCS; ++i) {
        if (allocations[i].active || allocations[i].ptr == 0) continue;
        uintptr_t aligned = align_ptr(allocations[i].ptr, alignment);
        if (aligned == UINTPTR_MAX || aligned != allocations[i].ptr) continue;
        if (aligned + size < aligned || aligned + size > allocations[i].ptr + allocations[i].size) continue;
        *out_ptr = aligned;
        return &allocations[i];
    }
    return NULL;
}

void heap_init(void) {
    current_page = NULL;
    current_offset = 0;
    rix_spin_init(&heap_lock);
    rix_lockdep_register("heap", 20u, &heap_lockdep_class);
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

    uint64_t irq;
    rix_spin_lock_irqsave(&heap_lock, &irq);
    (void)rix_lockdep_acquire(heap_lockdep_class);
    /* Reuse path: a freed block costs no new page and no new extent. */
    uintptr_t reuse_at = 0;
    heap_allocation_t *block = find_free_block(size, alignment, &reuse_at);
    if (block) {
        uint64_t tail = (uint64_t)(block->ptr + block->size) - (uint64_t)(reuse_at + size);
        heap_allocation_t *split = NULL;
        if (tail >= HEAP_MIN_SPLIT) {
            split = find_empty_record();
            if (split) {
                split->page = block->page;
                split->ptr = reuse_at + size;
                split->size = (size_t)tail;
                split->active = 0;
            }
        }
        block->ptr = reuse_at;
        block->size = size;
        block->active = 1;
        (void)rix_lockdep_release(heap_lockdep_class);
        rix_spin_unlock_irqrestore(&heap_lock, irq);
        return (void *)reuse_at;
    }

    /* Bump path: needs a never-used record (clobbering a free block
     * would leak its memory), else fail closed. */
    heap_allocation_t *record = find_empty_record();
    if (!record) {
        (void)rix_lockdep_release(heap_lockdep_class);
        rix_spin_unlock_irqrestore(&heap_lock, irq);
        return NULL;
    }

    uintptr_t base = (uintptr_t)current_page;
    uintptr_t aligned = align_ptr(base + current_offset, alignment);
    if (!current_page || aligned == UINTPTR_MAX ||
        aligned + size < aligned || aligned + size > base + HEAP_PAGE_SIZE) {
        uint64_t phys = pmm_alloc_page();
        if (!phys) {
            (void)rix_lockdep_release(heap_lockdep_class);
            rix_spin_unlock_irqrestore(&heap_lock, irq);
            return NULL;
        }
        current_page = (uint8_t *)(uintptr_t)phys;
        current_offset = 0;
        base = (uintptr_t)current_page;
        aligned = align_ptr(base, alignment);
        if (aligned == UINTPTR_MAX || aligned + size < aligned ||
            aligned + size > base + HEAP_PAGE_SIZE) {
            pmm_free_page(phys);
            current_page = NULL;
            current_offset = 0;
            (void)rix_lockdep_release(heap_lockdep_class);
            rix_spin_unlock_irqrestore(&heap_lock, irq);
            return NULL;
        }
    }

    record->page = (uintptr_t)current_page;
    record->ptr = aligned;
    record->size = size;
    record->active = 1;
    current_offset = (size_t)(aligned + size - (uintptr_t)current_page);
    (void)rix_lockdep_release(heap_lockdep_class);
    rix_spin_unlock_irqrestore(&heap_lock, irq);
    return (void *)aligned;
}

void kfree(void *ptr) {
    if (!ptr) return;
    uintptr_t address = (uintptr_t)ptr;
    uint64_t irq;
    rix_spin_lock_irqsave(&heap_lock, &irq);
    (void)rix_lockdep_acquire(heap_lockdep_class);
    for (size_t i = 0; i < HEAP_MAX_ALLOCS; ++i) {
        heap_allocation_t *record = &allocations[i];
        if (!record->active || record->ptr != address) continue;
        uintptr_t page = record->page;
        /* Keep ptr/size: the record becomes a reusable free block. */
        record->active = 0;
        if (!page_has_live_allocations(page)) {
            pmm_free_page(page);
            if ((uintptr_t)current_page == page) {
                current_page = NULL;
                current_offset = 0;
            }
            /* The page is gone: drop its blocks, else they dangle. */
            for (size_t j = 0; j < HEAP_MAX_ALLOCS; ++j) {
                if (allocations[j].page == page) {
                    allocations[j].page = 0;
                    allocations[j].ptr = 0;
                    allocations[j].size = 0;
                    allocations[j].active = 0;
                }
            }
        }
        (void)rix_lockdep_release(heap_lockdep_class);
        rix_spin_unlock_irqrestore(&heap_lock, irq);
        return;
    }
    (void)rix_lockdep_release(heap_lockdep_class);
    rix_spin_unlock_irqrestore(&heap_lock, irq);
}
