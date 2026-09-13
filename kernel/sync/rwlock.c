#include "rwlock.h"
#include "../process/process.h"
#include "../sched/scheduler.h"
#include <stddef.h>

void rix_rwlock_init(rix_rwlock_t *rw) {
    if (!rw) return;
    rix_spin_init(&rw->guard);
    rw->readers = 0;
    rw->waiting_writers = 0;
    rw->writer = 0;
    rw->write_locked = 0;
}

int rix_rwlock_read_trylock(rix_rwlock_t *rw) {
    if (!rw) return -1;
    uint64_t irq;
    rix_spin_lock_irqsave(&rw->guard, &irq);
    int rc;
    if (!rw->write_locked && rw->waiting_writers == 0) {
        rw->readers++;
        rc = 0;
    } else {
        rc = 1;
    }
    rix_spin_unlock_irqrestore(&rw->guard, irq);
    return rc;
}

int rix_rwlock_read_lock(rix_rwlock_t *rw) {
    if (!rw) return -1;
    for (;;) {
        int rc = rix_rwlock_read_trylock(rw);
        if (rc == 0 || rc < 0) return rc;
        scheduler_yield();
    }
}

int rix_rwlock_read_unlock(rix_rwlock_t *rw) {
    if (!rw) return -1;
    uint64_t irq;
    rix_spin_lock_irqsave(&rw->guard, &irq);
    int rc;
    if (rw->readers == 0) {
        rc = -3;
    } else {
        rw->readers--;
        rc = 0;
    }
    rix_spin_unlock_irqrestore(&rw->guard, irq);
    return rc;
}

int rix_rwlock_write_trylock(rix_rwlock_t *rw) {
    if (!rw) return -1;
    uint64_t self = (uint64_t)process_current();
    uint64_t irq;
    rix_spin_lock_irqsave(&rw->guard, &irq);
    int rc;
    if (rw->write_locked && rw->writer == self) {
        rc = -2;
    } else if (!rw->write_locked && rw->readers == 0) {
        rw->write_locked = 1;
        rw->writer = self;
        rc = 0;
    } else {
        rc = 1;
    }
    rix_spin_unlock_irqrestore(&rw->guard, irq);
    return rc;
}

int rix_rwlock_write_lock(rix_rwlock_t *rw) {
    if (!rw) return -1;
    uint64_t irq;
    rix_spin_lock_irqsave(&rw->guard, &irq);
    rw->waiting_writers++;
    rix_spin_unlock_irqrestore(&rw->guard, irq);
    for (;;) {
        int rc = rix_rwlock_write_trylock(rw);
        if (rc == 0 || rc < 0) {
            rix_spin_lock_irqsave(&rw->guard, &irq);
            rw->waiting_writers--;
            rix_spin_unlock_irqrestore(&rw->guard, irq);
            return rc;
        }
        scheduler_yield();
    }
}

int rix_rwlock_write_unlock(rix_rwlock_t *rw) {
    if (!rw) return -1;
    uint64_t self = (uint64_t)process_current();
    uint64_t irq;
    rix_spin_lock_irqsave(&rw->guard, &irq);
    int rc;
    if (!rw->write_locked || rw->writer != self) {
        rc = -3;
    } else {
        rw->write_locked = 0;
        rw->writer = 0;
        rc = 0;
    }
    rix_spin_unlock_irqrestore(&rw->guard, irq);
    return rc;
}
