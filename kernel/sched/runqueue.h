#pragma once
/* Phase R2 (Phase 06: per-CPU queues): bounded per-CPU runqueue.
 *
 * One runqueue per CPU holds the set of task-slot indices that CPU may
 * pick, as a bitmask plus a round-robin cursor. 32 task slots fit one
 * u32; every op is bounded (worst case one 32-step scan) and allocates
 * nothing.
 *
 * The mask is a CACHE of the scheduler's ground truth, not truth
 * itself: bit i may be set only while tasks[i] is RUNNABLE and eligible
 * on that CPU (BSP: any RUNNABLE; AP: RUNNABLE kernel-thread ap_ok,
 * never slot 0). scheduler.c maintains the cache at every transition
 * (create / yield-old / pick / block / abort / wake / death / ap_ok
 * flip) and re-verifies it at every pick (pick-verify-resync plus a
 * full rq_verify against task states — fail-stop on drift, so a missed
 * site bricks the boot loudly instead of starving a task silently).
 *
 * Locking: like thread.[hc], all ops assume sched_lock is held
 * (_locked suffix). No lock-free mask readers exist: every mutation
 * and every pick runs under sched_lock; runnable-count gates keep
 * their own task scan. */
#include <stddef.h>
#include <stdint.h>

#define RIX_RQ_SLOTS 32u
/* Phase R4: high-priority consecutive-pick cap (starvation guard). With
 * high work present, at most CAP high picks run back-to-back before one
 * normal pick is forced: high share <= CAP/(CAP+1) (80% at CAP=4) and
 * normal work always drains. */
#define RIX_RQ_HIPRI_CAP 4u

typedef struct {
    uint32_t mask;
    uint32_t cursor;
    uint32_t hipri_streak;
} rix_runqueue_t;

void rq_init_locked(rix_runqueue_t *rq);
void rq_add_locked(rix_runqueue_t *rq, uint32_t idx);
void rq_remove_locked(rix_runqueue_t *rq, uint32_t idx);
/* Next pick with priority preference. himask bit i = slot i is HIGH
 * priority (0 = all normal: pure round-robin, the pre-R4 behavior).
 * High-present + streak<CAP: round-robin among HIGH, streak++. Else a
 * normal round-robin pass over the whole mask, streak reset. Cursor
 * advances once per pick; bounded, allocates nothing. */
int rq_pick_locked(rix_runqueue_t *rq, uint32_t himask);
int rq_empty_locked(const rix_runqueue_t *rq);
uint32_t rq_count_locked(const rix_runqueue_t *rq);
int rq_contains_locked(const rix_runqueue_t *rq, uint32_t idx);
