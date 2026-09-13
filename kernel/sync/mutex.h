#pragma once
#include <stdint.h>
#include "lock.h"

/* Cooperative mutex with ownership tracking.
 *
 * Context model (matches this kernel today): lock() may yield while
 * contended, so it is for task context only; IRQ context must use the
 * try variant. lock_irqsave() additionally masks IRQs around the hold
 * for short sections shared with IRQ handlers. No recursive locking:
 * re-locking by the owner fails instead of deadlocking. Unlock by a
 * non-owner fails. P3 will convert the yield-spin backend to true
 * task blocking without changing this API.
 *
 * Owner identity is the pid (uint64_t); locked==0 means free, so pid
 * numbering is never confused with state.
 */
typedef struct {
    rix_spinlock_t guard;
    uint64_t owner;
    int locked;
} rix_mutex_t;

void rix_mutex_init(rix_mutex_t *m);
int rix_mutex_lock(rix_mutex_t *m);
int rix_mutex_trylock(rix_mutex_t *m);
int rix_mutex_lock_irqsave(rix_mutex_t *m, uint64_t *flags);
int rix_mutex_unlock(rix_mutex_t *m);
int rix_mutex_unlock_irqrestore(rix_mutex_t *m, uint64_t flags);
int rix_mutex_owner(const rix_mutex_t *m, uint64_t *out_owner);

/* 0 ok; -1 bad argument; 1 busy (try only); -2 recursive lock attempt;
 * -3 unlock by non-owner or of an unlocked mutex. */
