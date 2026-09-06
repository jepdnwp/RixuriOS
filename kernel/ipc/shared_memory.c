#include "shared_memory.h"
#include "../mm/pmm.h"
#include "../mm/vmm.h"
#include "../process/address_space.h"
#include <stddef.h>

#define SHM_PAGE_SIZE 4096ULL

typedef struct {
    uint8_t used;
    uint32_t refs;
    uint64_t size;
    uint64_t pages[RIX_SHM_MAX_PAGES];
} shm_object_t;

static shm_object_t objects[RIX_SHM_MAX];

int shm_init(void) {
    for (size_t i = 0; i < RIX_SHM_MAX; ++i) {
        objects[i].used = 0;
        objects[i].refs = 0;
        objects[i].size = 0;
        for (size_t j = 0; j < RIX_SHM_MAX_PAGES; ++j) objects[i].pages[j] = 0;
    }
    return 0;
}

static shm_object_t *lookup(rix_shm_id_t id) {
    return id < RIX_SHM_MAX && objects[id].used ? &objects[id] : NULL;
}

int shm_create(uint64_t size, rix_shm_id_t *out_id) {
    if (!size || size > RIX_SHM_MAX_PAGES * SHM_PAGE_SIZE || !out_id) return -1;
    uint64_t pages = (size + SHM_PAGE_SIZE - 1ULL) / SHM_PAGE_SIZE;
    for (rix_shm_id_t i = 1; i < RIX_SHM_MAX; ++i) {
        if (objects[i].used) continue;
        shm_object_t *o = &objects[i];
        o->used = 1;
        o->refs = 0;
        o->size = pages * SHM_PAGE_SIZE;
        for (uint64_t p = 0; p < pages; ++p) {
            o->pages[p] = pmm_alloc_page();
            if (!o->pages[p]) {
                for (uint64_t j = 0; j < p; ++j) pmm_free_page(o->pages[j]);
                o->used = 0;
                o->size = 0;
                return -1;
            }
        }
        *out_id = i;
        return 0;
    }
    return -1;
}

int shm_map(rix_shm_id_t id, pid_t pid, uint64_t va, uint64_t flags) {
    shm_object_t *o = lookup(id);
    rix_process_t *p = process_lookup(pid);
    if (!o || !p || !p->address_space.pml4_phys || !va || (va & (SHM_PAGE_SIZE - 1ULL)) ||
        va < 0x0000008000000000ULL || va >= 0x0000800000000000ULL ||
        o->size > 0x0000800000000000ULL - va) return -1;

    uint64_t mapped = 0;
    for (; mapped < o->size; mapped += SHM_PAGE_SIZE) {
        if (address_space_map_shared(&p->address_space, va + mapped,
                                     o->pages[mapped / SHM_PAGE_SIZE], flags) != 0) {
            for (uint64_t off = 0; off < mapped; off += SHM_PAGE_SIZE)
                (void)address_space_unmap(&p->address_space, va + off);
            return -1;
        }
    }
    o->refs++;
    return 0;
}

int shm_unmap(rix_shm_id_t id, pid_t pid, uint64_t va) {
    shm_object_t *o = lookup(id);
    rix_process_t *p = process_lookup(pid);
    if (!o || !p || !p->address_space.pml4_phys || !va ||
        (va & (SHM_PAGE_SIZE - 1ULL))) return -1;
    for (uint64_t off = 0; off < o->size; off += SHM_PAGE_SIZE) {
        uint64_t flags = address_space_query_flags(&p->address_space, va + off);
        if (!(flags & RIXURI_PTE_PRESENT) || (flags & RIXURI_PTE_OWNED) ||
            address_space_translate(&p->address_space, va + off) !=
                o->pages[off / SHM_PAGE_SIZE]) return -1;
    }
    for (uint64_t off = 0; off < o->size; off += SHM_PAGE_SIZE) {
        if (address_space_unmap(&p->address_space, va + off) != 0) return -1;
    }
    if (o->refs) o->refs--;
    return 0;
}

int shm_destroy(rix_shm_id_t id) {
    shm_object_t *o = lookup(id);
    if (!o || o->refs) return -1;
    for (uint64_t i = 0; i < o->size / SHM_PAGE_SIZE; ++i)
        if (o->pages[i]) pmm_free_page(o->pages[i]);
    o->used = 0;
    o->size = 0;
    return 0;
}

uint64_t shm_size(rix_shm_id_t id) {
    shm_object_t *o = lookup(id);
    return o ? o->size : 0;
}
