#pragma once
#include <stdint.h>
#include "lock.h"

/* Cooperative counting semaphore.
 *
 * down() may yield (task context only); trydown() is safe anywhere.
 * down_timeout() polls the monotonic clock while yielding, matching
 * the existing nanosleep cooperation model: the expiry bound is
 * honored, wakeup latency is not realtime. up() past max fails
 * instead of silently capping (caller bug signal).
 *
 * 0 ok; -1 bad argument; 1 empty (try) / expired (timeout);
 * -2 count would exceed max.
 */
typedef struct {
    rix_spinlock_t guard;
    uint32_t count;
    uint32_t max;
} rix_sem_t;

void rix_sem_init(rix_sem_t *s, uint32_t initial, uint32_t max);
int rix_sem_down(rix_sem_t *s);
int rix_sem_trydown(rix_sem_t *s);
int rix_sem_down_timeout(rix_sem_t *s, uint64_t timeout_ns);
int rix_sem_up(rix_sem_t *s);
int rix_sem_count(const rix_sem_t *s, uint32_t *out);
