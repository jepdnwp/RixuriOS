#pragma once
#include <stdint.h>

/* Intrusive atomic reference count for shared kernel objects.
 * The holder initializes to 1. acquire() on a zero count fails
 * (resurrecting a dead object is a bug, not a feature). release()
 * returns 1 exactly once, when the count reaches zero, telling the
 * caller it now owns teardown; 0 means references remain. Underflow
 * is reported, never wrapped. Header-only; freestanding-safe. */
typedef struct {
    volatile uint64_t refs;
} rix_refcount_t;

static inline void rix_ref_init(rix_refcount_t *r) {
    if (r) __atomic_store_n(&r->refs, 1u, __ATOMIC_RELAXED);
}

static inline int rix_ref_acquire(rix_refcount_t *r) {
    uint64_t n;
    if (!r) return -1;
    do {
        n = __atomic_load_n(&r->refs, __ATOMIC_ACQUIRE);
        if (!n) return -1;
    } while (!__atomic_compare_exchange_n(&r->refs, &n, n + 1u, 0,
                                          __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE));
    return 0;
}

static inline int rix_ref_release(rix_refcount_t *r) {
    uint64_t before;
    if (!r) return -1;
    before = __atomic_fetch_sub(&r->refs, 1u, __ATOMIC_ACQ_REL);
    if (!before) {
        __atomic_store_n(&r->refs, 0u, __ATOMIC_RELAXED);
        return -1;
    }
    return before == 1u ? 1 : 0;
}
