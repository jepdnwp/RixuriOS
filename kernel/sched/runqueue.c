#include "runqueue.h"

/* Pure bitmask logic: no locking inside (caller holds sched_lock),
 * no external dependencies — trivially host-testable. */

void rq_init_locked(rix_runqueue_t *rq) {
    if (!rq) return;
    rq->mask = 0;
    rq->cursor = 0;
    rq->hipri_streak = 0;
}

void rq_add_locked(rix_runqueue_t *rq, uint32_t idx) {
    if (!rq || idx >= RIX_RQ_SLOTS) return;
    rq->mask |= (1u << idx);
}

void rq_remove_locked(rix_runqueue_t *rq, uint32_t idx) {
    if (!rq || idx >= RIX_RQ_SLOTS) return;
    rq->mask &= ~(1u << idx);
}

/* First set bit in pool at/after start (wrapping once). pool is
 * nonzero; the scan always hits. Advances the cursor past the pick. */
static int rq_scan_locked(rix_runqueue_t *rq, uint32_t pool, uint32_t start) {
    for (uint32_t n = 0; n < RIX_RQ_SLOTS; n++) {
        uint32_t i = (start + n) % RIX_RQ_SLOTS;
        if (pool & (1u << i)) {
            rq->cursor = (i + 1u) % RIX_RQ_SLOTS;
            return (int)i;
        }
    }
    return -1;
}

/* Round-robin with high-priority preference (see header). Out-of-range
 * cursor values fold back into range. A NULL queue or an empty mask
 * picks -1 without touching state. */
int rq_pick_locked(rix_runqueue_t *rq, uint32_t himask) {
    uint32_t start, pool;
    int pick;
    if (!rq || !rq->mask) return -1;
    start = rq->cursor % RIX_RQ_SLOTS;
    pool = rq->mask & himask;
    if (pool && rq->hipri_streak < RIX_RQ_HIPRI_CAP) {
        pick = rq_scan_locked(rq, pool, start);
        if (pick >= 0) {
            rq->hipri_streak++;
            return pick;
        }
    }
    /* Normal pass (also the all-high and streak-capped cases): whole
     * mask rotation, streak reset. pool==mask here is possible and
     * fine — the pass is defined over the mask, not over "normal". */
    pick = rq_scan_locked(rq, rq->mask, start);
    rq->hipri_streak = 0;
    return pick;
}

int rq_empty_locked(const rix_runqueue_t *rq) {
    return !rq || !rq->mask;
}

uint32_t rq_count_locked(const rix_runqueue_t *rq) {
    uint32_t m, n = 0;
    if (!rq) return 0;
    m = rq->mask;
    while (m) {
        n += m & 1u;
        m >>= 1;
    }
    return n;
}

int rq_contains_locked(const rix_runqueue_t *rq, uint32_t idx) {
    if (!rq || idx >= RIX_RQ_SLOTS) return 0;
    return (rq->mask & (1u << idx)) != 0;
}
