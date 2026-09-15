#include "klog.h"
#include "../sync/lock.h"
#include <stddef.h>
#include <stdint.h>

/* Bounded kernel diagnostic ring (Phase 19 klog).
 * Byte-stream with ever-increasing sequence numbers: byte i lives at
 * ring[i % KLOG_SIZE] while i >= oldest. Push is irqsave + trylock only:
 * fault/IRQ writers (IST stacks, PIT preempts) must never block — on
 * contention the message is dropped and counted, never hung. No allocation,
 * no nested logging, no lockdep (leaf, unwired by design). */
static uint8_t klog_ring[KLOG_SIZE];
static uint64_t klog_seq;
static uint64_t klog_drop;
static rix_spinlock_t klog_lock;
static int klog_ready;

static void klog_ensure(void) {
    if (klog_ready) return;
    rix_spin_init(&klog_lock);
    klog_ready = 1;
}

void klog_push(const char *data, size_t length) {
    if (!data || !length) return;
    klog_ensure();
    uint64_t irq = rix_irq_save();
    if (!rix_spin_trylock(&klog_lock)) {
        klog_drop += (uint64_t)length;
        rix_irq_restore(irq);
        return;
    }
    for (size_t i = 0; i < length; i++) {
        klog_ring[klog_seq % KLOG_SIZE] = (uint8_t)data[i];
        klog_seq++;
    }
    rix_spin_unlock(&klog_lock);
    rix_irq_restore(irq);
}

uint64_t klog_write_seq(void) {
    klog_ensure();
    return __atomic_load_n(&klog_seq, __ATOMIC_ACQUIRE);
}

uint64_t klog_oldest(void) {
    uint64_t seq = klog_write_seq();
    return seq > KLOG_SIZE ? seq - KLOG_SIZE : 0u;
}

size_t klog_copy(uint64_t start, uint8_t *dst, size_t count) {
    if (!dst || !count) return 0;
    klog_ensure();
    uint64_t irq = rix_irq_save();
    if (!rix_spin_trylock(&klog_lock)) {
        rix_irq_restore(irq);
        return 0;
    }
    uint64_t seq = klog_seq;
    uint64_t oldest = seq > KLOG_SIZE ? seq - KLOG_SIZE : 0u;
    if (start < oldest) start = oldest;
    if (start >= seq) {
        rix_spin_unlock(&klog_lock);
        rix_irq_restore(irq);
        return 0;
    }
    uint64_t avail = seq - start;
    if (count > avail) count = (size_t)avail;
    for (size_t i = 0; i < count; i++)
        dst[i] = klog_ring[(start + i) % KLOG_SIZE];
    rix_spin_unlock(&klog_lock);
    rix_irq_restore(irq);
    return count;
}

uint64_t klog_dropped(void) {
    return __atomic_load_n(&klog_drop, __ATOMIC_ACQUIRE);
}
