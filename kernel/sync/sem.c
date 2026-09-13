#include "sem.h"
#include "../sched/scheduler.h"
#include "../time/time.h"
#include <stddef.h>

void rix_sem_init(rix_sem_t *s, uint32_t initial, uint32_t max) {
    if (!s) return;
    rix_spin_init(&s->guard);
    if (initial > max) initial = max;
    s->count = initial;
    s->max = max ? max : 1u;
    if (s->count > s->max) s->count = s->max;
}

int rix_sem_trydown(rix_sem_t *s) {
    if (!s) return -1;
    uint64_t irq;
    rix_spin_lock_irqsave(&s->guard, &irq);
    int rc;
    if (s->count > 0) {
        s->count--;
        rc = 0;
    } else {
        rc = 1;
    }
    rix_spin_unlock_irqrestore(&s->guard, irq);
    return rc;
}

int rix_sem_down(rix_sem_t *s) {
    if (!s) return -1;
    for (;;) {
        int rc = rix_sem_trydown(s);
        if (rc == 0 || rc < 0) return rc;
        scheduler_yield();
    }
}

int rix_sem_down_timeout(rix_sem_t *s, uint64_t timeout_ns) {
    if (!s) return -1;
    uint64_t start = time_monotonic_ns();
    uint64_t deadline = start + timeout_ns;
    if (deadline < start) deadline = UINT64_MAX;
    for (;;) {
        int rc = rix_sem_trydown(s);
        if (rc == 0 || rc < 0) return rc;
        if (time_monotonic_ns() >= deadline) return 1;
        scheduler_yield();
    }
}

int rix_sem_up(rix_sem_t *s) {
    if (!s) return -1;
    uint64_t irq;
    rix_spin_lock_irqsave(&s->guard, &irq);
    int rc;
    if (s->count >= s->max) {
        rc = -2;
    } else {
        s->count++;
        rc = 0;
    }
    rix_spin_unlock_irqrestore(&s->guard, irq);
    return rc;
}

int rix_sem_count(const rix_sem_t *s, uint32_t *out) {
    if (!s || !out) return -1;
    *out = s->count;
    return 0;
}
