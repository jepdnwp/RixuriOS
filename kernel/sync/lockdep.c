#include "lockdep.h"
#include "lock.h"
#include "../serial.h"
#include <stddef.h>

__attribute__((weak)) int lockdep_cpu_index(void) { return 0; }

static rix_spinlock_t registry_lock;
static const char *class_names[RIX_LOCKDEP_MAX_CLASSES];
static unsigned class_ranks[RIX_LOCKDEP_MAX_CLASSES];
static unsigned class_count = 1u;
static unsigned violations;
static unsigned held_stack[RIX_LOCKDEP_CPUS][RIX_LOCKDEP_MAX_DEPTH];
static unsigned held_depth[RIX_LOCKDEP_CPUS];

static unsigned cpu_slot(void) {
    int id = lockdep_cpu_index();
    if (id < 0 || (unsigned)id >= RIX_LOCKDEP_CPUS) id = 0;
    return (unsigned)id;
}

static void warn_order(const char *what, unsigned klass, unsigned top) {
    serial_write("LOCKDEP: ");
    serial_write(what);
    serial_write(" class=");
    serial_write(rix_lockdep_name(klass));
    serial_write(" top=");
    serial_write(rix_lockdep_name(top));
    serial_write("\r\n");
    __atomic_fetch_add(&violations, 1u, __ATOMIC_RELAXED);
}

int rix_lockdep_register(const char *name, unsigned rank, unsigned *out_class) {
    if (!name || !name[0] || !out_class) return -1;
    uint64_t irq;
    rix_spin_lock_irqsave(&registry_lock, &irq);
    if (class_count >= RIX_LOCKDEP_MAX_CLASSES) {
        rix_spin_unlock_irqrestore(&registry_lock, irq);
        return -1;
    }
    unsigned id = class_count++;
    class_names[id] = name;
    class_ranks[id] = rank;
    *out_class = id;
    rix_spin_unlock_irqrestore(&registry_lock, irq);
    return 0;
}

int rix_lockdep_acquire(unsigned klass) {
    if (klass == RIX_LOCKDEP_UNTRACKED) return 0;
    if (klass >= RIX_LOCKDEP_MAX_CLASSES || !class_names[klass]) return -1;
    /* The per-CPU stack is mutated without a lock; mask IRQs so an
     * IRQ-path acquisition on this CPU cannot interleave. Registration
     * itself must finish before SMP bringup (read-only afterwards). */
    uint64_t irq = rix_irq_save();
    unsigned cpu = cpu_slot();
    int rc = 0;
    if (held_depth[cpu] >= RIX_LOCKDEP_MAX_DEPTH) {
        warn_order("depth-overflow", klass, held_stack[cpu][RIX_LOCKDEP_MAX_DEPTH - 1u]);
        rc = -2;
    } else if (held_depth[cpu] > 0) {
        unsigned top = held_stack[cpu][held_depth[cpu] - 1u];
        if (top >= RIX_LOCKDEP_MAX_CLASSES || !class_names[top] ||
            class_ranks[klass] <= class_ranks[top]) {
            warn_order("order-violation", klass, top);
            rc = -2;
        } else {
            held_stack[cpu][held_depth[cpu]++] = klass;
        }
    } else {
        held_stack[cpu][held_depth[cpu]++] = klass;
    }
    rix_irq_restore(irq);
    return rc;
}

int rix_lockdep_release(unsigned klass) {
    if (klass == RIX_LOCKDEP_UNTRACKED) return 0;
    if (klass >= RIX_LOCKDEP_MAX_CLASSES || !class_names[klass]) return -1;
    uint64_t irq = rix_irq_save();
    unsigned cpu = cpu_slot();
    int rc = 0;
    if (held_depth[cpu] == 0 || held_stack[cpu][held_depth[cpu] - 1u] != klass) {
        unsigned top = held_depth[cpu] ? held_stack[cpu][held_depth[cpu] - 1u] : 0u;
        warn_order("release-mismatch", klass, top);
        rc = -2;
    } else {
        held_depth[cpu]--;
    }
    rix_irq_restore(irq);
    return rc;
}

unsigned rix_lockdep_violations(void) {
    return __atomic_load_n(&violations, __ATOMIC_RELAXED);
}

const char *rix_lockdep_name(unsigned klass) {
    if (klass < RIX_LOCKDEP_MAX_CLASSES && class_names[klass]) return class_names[klass];
    return "?";
}
