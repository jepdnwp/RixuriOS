#include "mutex.h"
#include "../process/process.h"
#include "../sched/scheduler.h"
#include <stddef.h>

void rix_mutex_init(rix_mutex_t *m) {
    if (!m) return;
    rix_spin_init(&m->guard);
    m->owner = 0;
    m->locked = 0;
}

static pid_t self_pid(void) { return process_current(); }

int rix_mutex_trylock(rix_mutex_t *m) {
    if (!m) return -1;
    uint64_t irq;
    rix_spin_lock_irqsave(&m->guard, &irq);
    int rc;
    if (!m->locked) {
        m->locked = 1;
        m->owner = (uint64_t)self_pid();
        rc = 0;
    } else if (m->owner == (uint64_t)self_pid()) {
        rc = -2;
    } else {
        rc = 1;
    }
    rix_spin_unlock_irqrestore(&m->guard, irq);
    return rc;
}

int rix_mutex_lock(rix_mutex_t *m) {
    if (!m) return -1;
    for (;;) {
        int rc = rix_mutex_trylock(m);
        if (rc == 0 || rc < 0) return rc;
        scheduler_yield();
    }
}

int rix_mutex_lock_irqsave(rix_mutex_t *m, uint64_t *flags) {
    if (!m) return -1;
    uint64_t saved = rix_irq_save();
    if (flags) *flags = saved;
    for (;;) {
        int rc = rix_mutex_trylock(m);
        if (rc == 0 || rc < 0) {
            if (rc != 0) rix_irq_restore(saved);
            return rc;
        }
        scheduler_yield();
    }
}

int rix_mutex_unlock(rix_mutex_t *m) {
    if (!m) return -1;
    uint64_t irq;
    rix_spin_lock_irqsave(&m->guard, &irq);
    int rc;
    if (!m->locked || m->owner != (uint64_t)self_pid()) {
        rc = -3;
    } else {
        m->locked = 0;
        m->owner = 0;
        rc = 0;
    }
    rix_spin_unlock_irqrestore(&m->guard, irq);
    return rc;
}

int rix_mutex_unlock_irqrestore(rix_mutex_t *m, uint64_t flags) {
    int rc = rix_mutex_unlock(m);
    rix_irq_restore(flags);
    return rc;
}

int rix_mutex_owner(const rix_mutex_t *m, uint64_t *out_owner) {
    if (!m || !out_owner) return -1;
    *out_owner = m->locked ? m->owner : 0;
    return 0;
}
