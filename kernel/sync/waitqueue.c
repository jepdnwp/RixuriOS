#include "waitqueue.h"
#include <stddef.h>

void rix_waitqueue_init(rix_waitqueue_t *queue) {
    if (!queue) return;
    rix_spin_init(&queue->lock);
    queue->count = 0;
    queue->generation = 1u;
    for (uint32_t i = 0; i < RIX_WQ_MAX_WAITERS; i++) {
        queue->waiters[i].id = 0;
        queue->waiters[i].generation = 0;
        queue->waiters[i].state = RIX_WAIT_UNUSED;
    }
}

static rix_waiter_t *resolve(rix_waitqueue_t *queue, rix_wait_handle_t handle) {
    if (!queue || handle.index >= RIX_WQ_MAX_WAITERS) return NULL;
    rix_waiter_t *w = &queue->waiters[handle.index];
    if (w->state == RIX_WAIT_UNUSED || w->generation != handle.generation) return NULL;
    return w;
}

int rix_waitqueue_prepare(rix_waitqueue_t *queue, uint64_t id, rix_wait_handle_t *out) {
    if (!queue || !out) return -1;
    uint64_t irq;
    rix_spin_lock_irqsave(&queue->lock, &irq);
    for (uint32_t i = 0; i < RIX_WQ_MAX_WAITERS; i++) {
        if (queue->waiters[i].state != RIX_WAIT_UNUSED) continue;
        queue->waiters[i].id = id;
        queue->waiters[i].generation = queue->generation++;
        if (queue->generation == 0) queue->generation = 1u;
        queue->waiters[i].state = RIX_WAIT_READY;
        queue->count++;
        out->index = i;
        out->generation = queue->waiters[i].generation;
        rix_spin_unlock_irqrestore(&queue->lock, irq);
        return 0;
    }
    rix_spin_unlock_irqrestore(&queue->lock, irq);
    return -1;
}

int rix_waitqueue_block(rix_waitqueue_t *queue, rix_wait_handle_t handle) {
    if (!queue) return -1;
    uint64_t irq;
    rix_spin_lock_irqsave(&queue->lock, &irq);
    rix_waiter_t *w = resolve(queue, handle);
    if (!w || w->state != RIX_WAIT_READY) {
        rix_spin_unlock_irqrestore(&queue->lock, irq);
        return -1;
    }
    w->state = RIX_WAIT_BLOCKED;
    rix_spin_unlock_irqrestore(&queue->lock, irq);
    return 0;
}

int rix_waitqueue_take_wakeup(rix_waitqueue_t *queue, rix_wait_handle_t handle) {
    if (!queue) return -1;
    uint64_t irq;
    rix_spin_lock_irqsave(&queue->lock, &irq);
    rix_waiter_t *w = resolve(queue, handle);
    if (!w) {
        rix_spin_unlock_irqrestore(&queue->lock, irq);
        return -1;
    }
    int rc = 1;
    if (w->state == RIX_WAIT_WOKEN) {
        w->state = RIX_WAIT_READY;
        rc = 0;
    }
    rix_spin_unlock_irqrestore(&queue->lock, irq);
    return rc;
}

int rix_waitqueue_wake_one(rix_waitqueue_t *queue) {
    if (!queue) return -1;
    uint64_t irq;
    rix_spin_lock_irqsave(&queue->lock, &irq);
    int rc = 1;
    for (uint32_t i = 0; i < RIX_WQ_MAX_WAITERS; i++) {
        if (queue->waiters[i].state == RIX_WAIT_BLOCKED) {
            queue->waiters[i].state = RIX_WAIT_WOKEN;
            rc = 0;
            break;
        }
    }
    rix_spin_unlock_irqrestore(&queue->lock, irq);
    return rc;
}

int rix_waitqueue_wake_all(rix_waitqueue_t *queue) {
    if (!queue) return -1;
    uint64_t irq;
    rix_spin_lock_irqsave(&queue->lock, &irq);
    int n = 0;
    for (uint32_t i = 0; i < RIX_WQ_MAX_WAITERS; i++) {
        if (queue->waiters[i].state == RIX_WAIT_BLOCKED) {
            queue->waiters[i].state = RIX_WAIT_WOKEN;
            n++;
        }
    }
    rix_spin_unlock_irqrestore(&queue->lock, irq);
    return n;
}

int rix_waitqueue_remove(rix_waitqueue_t *queue, rix_wait_handle_t handle) {
    if (!queue) return -1;
    uint64_t irq;
    rix_spin_lock_irqsave(&queue->lock, &irq);
    rix_waiter_t *w = resolve(queue, handle);
    if (!w) {
        rix_spin_unlock_irqrestore(&queue->lock, irq);
        return -1;
    }
    w->id = 0;
    w->state = RIX_WAIT_UNUSED;
    if (queue->count) queue->count--;
    rix_spin_unlock_irqrestore(&queue->lock, irq);
    return 0;
}
