#pragma once
#include <stdint.h>
#include "lock.h"

/* Bounded waiter registry with generation-counted handles.
 *
 * Handles (not pointers) cross the API: a stale handle from a
 * removed-and-reused slot is rejected instead of aliasing another
 * waiter. Lifecycle: prepare -> block -> (wake_one/all by others) ->
 * take_wakeup -> remove. take_wakeup consumes exactly one wakeup.
 *
 * This registry tracks waiter STATE only. Binding a wakeup to a real
 * scheduler task transition (BLOCKED sleep/wakeup with timeout and
 * cancellation) is P3 work built on these states; until then callers
 * poll take_wakeup across yields, same cooperation model as nanosleep.
 * 0 ok; -1 bad argument or stale handle; 1 waiter slot for
 * take_wakeup means "no wakeup pending".
 */
#define RIX_WQ_MAX_WAITERS 64u

typedef enum { RIX_WAIT_UNUSED = 0, RIX_WAIT_READY = 1, RIX_WAIT_BLOCKED = 2, RIX_WAIT_WOKEN = 3 } rix_wait_state_t;

typedef struct {
    uint32_t index;
    uint32_t generation;
} rix_wait_handle_t;

typedef struct {
    uint64_t id;
    uint32_t generation;
    volatile rix_wait_state_t state;
} rix_waiter_t;

typedef struct {
    rix_spinlock_t lock;
    rix_waiter_t waiters[RIX_WQ_MAX_WAITERS];
    uint32_t count;
    uint32_t generation;
} rix_waitqueue_t;

void rix_waitqueue_init(rix_waitqueue_t *queue);
int rix_waitqueue_prepare(rix_waitqueue_t *queue, uint64_t id, rix_wait_handle_t *out);
int rix_waitqueue_block(rix_waitqueue_t *queue, rix_wait_handle_t handle);
int rix_waitqueue_take_wakeup(rix_waitqueue_t *queue, rix_wait_handle_t handle);
int rix_waitqueue_wake_one(rix_waitqueue_t *queue);
int rix_waitqueue_wake_all(rix_waitqueue_t *queue);
int rix_waitqueue_remove(rix_waitqueue_t *queue, rix_wait_handle_t handle);
