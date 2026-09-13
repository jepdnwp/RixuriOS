#include "../kernel/mm/heap.h"
#include "../kernel/mm/pmm.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>

/* Mirrors HEAP_MAX_ALLOCS in kernel/mm/heap.c (record table bound). */
#define TEST_HEAP_MAX_ALLOCS 256u

static void put32(uint8_t *entry, size_t offset, uint32_t value) {
    memcpy(entry + offset, &value, sizeof(value));
}

static void put64(uint8_t *entry, size_t offset, uint64_t value) {
    memcpy(entry + offset, &value, sizeof(value));
}

int main(void) {
    uint8_t memory_map[40];
    memset(memory_map, 0, sizeof(memory_map));
    put32(memory_map, 0u, 7u);
    put64(memory_map, 8u, 0u);
    put64(memory_map, 24u, 1024u);
    pmm_init(memory_map, sizeof(memory_map), sizeof(memory_map),
             0x100000u, 0x101000u, 0x102000u, 0x1000u);
    heap_init();

    uint64_t before = pmm_free_pages();
    void *a = kmalloc(16u, 16u);
    void *b = kmalloc(32u, 16u);
    assert(a != NULL && b != NULL);
    assert(pmm_free_pages() == before - 1u);

    kfree(a);
    assert(pmm_free_pages() == before - 1u);
    kfree(a); /* double free must be harmless */
    assert(pmm_free_pages() == before - 1u);
    kfree(b);
    assert(pmm_free_pages() == before);

    /* Freed blocks are reused in place: no new page is consumed and
     * the same address comes back while a sibling keeps the page. */
    a = kmalloc(16u, 16u);
    b = kmalloc(32u, 16u);
    assert(a != NULL && b != NULL);
    assert(pmm_free_pages() == before - 1u);
    kfree(a);
    void *c = kmalloc(16u, 16u);
    assert(c == a);
    assert(pmm_free_pages() == before - 1u);

    /* Split tails are tracked too: freeing a 64-byte block serves two
     * later 16-byte allocations from head then tail. */
    kfree(b);
    kfree(c);
    assert(pmm_free_pages() == before);
    a = kmalloc(64u, 16u);
    b = kmalloc(16u, 16u);
    assert(a != NULL && b != NULL && b == (void *)((uintptr_t)a + 64u));
    kfree(a);
    c = kmalloc(16u, 16u);
    assert(c == a);
    void *d = kmalloc(16u, 16u);
    assert(d == (void *)((uintptr_t)a + 16u));
    assert(pmm_free_pages() == before - 1u);
    kfree(b);
    kfree(c);
    kfree(d);
    assert(pmm_free_pages() == before);

    /* Record exhaustion fails closed; freeing everything reclaims. */
    void *v[TEST_HEAP_MAX_ALLOCS + 1];
    size_t n = 0;
    while (n <= TEST_HEAP_MAX_ALLOCS) {
        v[n] = kmalloc(8u, 8u);
        if (!v[n]) break;
        ++n;
    }
    assert(n == TEST_HEAP_MAX_ALLOCS);
    assert(kmalloc(8u, 8u) == NULL);
    for (size_t i = 0; i < n; ++i) kfree(v[i]);
    assert(pmm_free_pages() == before);

    assert(kmalloc(4097u, 16u) == NULL);
    assert(pmm_free_pages() == before);
    kfree((void *)(uintptr_t)0x12345000u);
    return 0;
}
