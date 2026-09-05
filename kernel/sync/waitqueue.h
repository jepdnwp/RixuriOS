#pragma once
#include <stdint.h>
#include "lock.h"

#define RIX_WQ_MAX_WAITERS 64

typedef enum { RIX_WAIT_UNUSED=0, RIX_WAIT_READY=1, RIX_WAIT_BLOCKED=2, RIX_WAIT_WOKEN=3 } rix_wait_state_t;
typedef struct { uint64_t id; volatile rix_wait_state_t state; } rix_waiter_t;
typedef struct { rix_spinlock_t lock; rix_waiter_t waiters[RIX_WQ_MAX_WAITERS]; uint32_t count; } rix_waitqueue_t;

void rix_waitqueue_init(rix_waitqueue_t *queue);
rix_waiter_t *rix_waitqueue_prepare(rix_waitqueue_t *queue, uint64_t id);
int rix_waitqueue_block(rix_waitqueue_t *queue, rix_waiter_t *waiter);
void rix_waitqueue_wake_one(rix_waitqueue_t *queue);
void rix_waitqueue_wake_all(rix_waitqueue_t *queue);
void rix_waitqueue_remove(rix_waitqueue_t *queue, rix_waiter_t *waiter);
