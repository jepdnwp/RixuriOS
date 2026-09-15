/* Phase R2/R4 host test: per-CPU runqueue mask/cursor logic plus
 * priority preference with starvation cap (kernel/sched/runqueue.c).
 * No stubs needed — the module is pure. */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "kernel/sched/runqueue.h"

int main(void) {
    rix_runqueue_t rq;

    rq_init_locked(&rq);
    assert(rq_empty_locked(&rq));
    assert(rq_count_locked(&rq) == 0);
    assert(rq_pick_locked(&rq, 0) == -1);
    assert(rq_pick_locked(0, 0) == -1);
    assert(rq_pick_locked(&rq, 0xFFFFFFFFu) == -1);
    assert(rq_empty_locked(0));
    assert(rq_count_locked(0) == 0);
    assert(!rq_contains_locked(&rq, 0));

    /* Out-of-range indices are ignored, never corrupt. */
    rq_add_locked(&rq, 32);
    rq_add_locked(&rq, 99);
    rq_add_locked(0, 1);
    assert(rq_empty_locked(&rq));
    rq_remove_locked(&rq, 32);
    rq_remove_locked(0, 1);

    /* Round-robin order follows insertion-independent cursor rotation:
     * adds are a set; picks rotate 1,2,3,1,2,3... */
    rq_add_locked(&rq, 3);
    rq_add_locked(&rq, 1);
    rq_add_locked(&rq, 2);
    assert(rq_count_locked(&rq) == 3);
    assert(rq_contains_locked(&rq, 2));
    assert(!rq_contains_locked(&rq, 0));
    assert(rq_pick_locked(&rq, 0) == 1);
    assert(rq_pick_locked(&rq, 0) == 2);
    assert(rq_pick_locked(&rq, 0) == 3);
    assert(rq_pick_locked(&rq, 0) == 1);
    /* Double-add is idempotent (still one rotation step per slot). */
    rq_add_locked(&rq, 2);
    assert(rq_count_locked(&rq) == 3);
    assert(rq_pick_locked(&rq, 0) == 2);

    /* Removal narrows the rotation; removing the cursor slot is safe. */
    rq_remove_locked(&rq, 3);
    assert(rq_count_locked(&rq) == 2);
    assert(rq_pick_locked(&rq, 0) == 1);
    assert(rq_pick_locked(&rq, 0) == 2);
    rq_remove_locked(&rq, 1);
    rq_remove_locked(&rq, 2);
    assert(rq_empty_locked(&rq));
    assert(rq_pick_locked(&rq, 0) == -1);
    /* Remove-absent is a no-op. */
    rq_remove_locked(&rq, 7);
    assert(rq_empty_locked(&rq));

    /* Wrap: high slots rotate back to low ones. */
    rq_init_locked(&rq);
    rq_add_locked(&rq, 31);
    rq_add_locked(&rq, 0);
    assert(rq_pick_locked(&rq, 0) == 0);
    assert(rq_pick_locked(&rq, 0) == 31);
    assert(rq_pick_locked(&rq, 0) == 0);

    /* A stale cursor (e.g. slots drained while it pointed high) folds
     * back into range instead of wedging the scan. */
    rq.cursor = 29;
    rq_remove_locked(&rq, 31);
    rq_remove_locked(&rq, 0);
    rq_add_locked(&rq, 5);
    assert(rq_pick_locked(&rq, 0) == 5);
    assert(rq_pick_locked(&rq, 0) == 5);

    /* Phase R4: high-priority preference. himask bit = HIGH slot. */
    rq_init_locked(&rq);
    rq_add_locked(&rq, 1);
    rq_add_locked(&rq, 2);
    rq_add_locked(&rq, 3);
    /* Slot 3 HIGH: preferred regardless of cursor position. */
    assert(rq_pick_locked(&rq, 1u << 3) == 3);
    assert(rq_pick_locked(&rq, 1u << 3) == 3);
    assert(rq_pick_locked(&rq, 1u << 3) == 3);
    assert(rq_pick_locked(&rq, 1u << 3) == 3);
    /* Streak cap (4): the 5th consecutive high-present pick is forced
     * to the normal rotation (slot 1 here: cursor sits past 3). */
    assert(rq_pick_locked(&rq, 1u << 3) == 1);
    /* Streak reset by the normal pass: high preferred again. */
    assert(rq_pick_locked(&rq, 1u << 3) == 3);
    /* himask naming non-member slots grants nothing. */
    assert(rq_pick_locked(&rq, 1u << 9) == 1);

    /* All-HIGH set rotates (streak cap still interleaves a reset pass,
     * which lands on HIGH too — no starvation, no wedge). */
    rq_init_locked(&rq);
    rq_add_locked(&rq, 4);
    rq_add_locked(&rq, 5);
    {
        uint32_t himask = (1u << 4) | (1u << 5);
        assert(rq_pick_locked(&rq, himask) == 4);
        assert(rq_pick_locked(&rq, himask) == 5);
        assert(rq_pick_locked(&rq, himask) == 4);
        assert(rq_pick_locked(&rq, himask) == 5);
        assert(rq_pick_locked(&rq, himask) == 4);
        assert(rq_pick_locked(&rq, himask) == 5);
    }

    /* HIGH subset rotates among several HIGH slots (not head-lined). */
    rq_init_locked(&rq);
    rq_add_locked(&rq, 0);
    rq_add_locked(&rq, 6);
    rq_add_locked(&rq, 7);
    rq_add_locked(&rq, 8);
    {
        uint32_t himask = (1u << 6) | (1u << 8);
        assert(rq_pick_locked(&rq, himask) == 6);
        assert(rq_pick_locked(&rq, himask) == 8);
        assert(rq_pick_locked(&rq, himask) == 6);
        assert(rq_pick_locked(&rq, himask) == 8);
    }

    printf("runqueue_test: PASS\n");
    return 0;
}
