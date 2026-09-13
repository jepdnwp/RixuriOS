#include "kernel/sync/mutex.h"
#include "kernel/sync/rwlock.h"
#include "kernel/sync/sem.h"
#include "kernel/sync/refcount.h"
#include "kernel/sync/lockdep.h"
#include "kernel/sync/waitqueue.h"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>

/* Phase P2 host coverage: mutex/RW/sem/refcount/waitqueue/lockdep
 * single-threaded semantics. Contended blocking paths spin on
 * scheduler_yield by design and cannot complete without a second
 * task, so they are covered by code inspection plus QEMU workloads,
 * not faked here. */

static uint64_t stub_pid = 1u;
static unsigned stub_yields;
static unsigned stub_serial_calls;

uint64_t process_current(void) { return stub_pid; }
void scheduler_yield(void) { ++stub_yields; }
uint64_t time_monotonic_ns(void) { return 1000u; }
void serial_write(const char *s) { (void)s; ++stub_serial_calls; }

static void mutex_checks(void) {
    rix_mutex_t m;
    uint64_t owner = 0;
    rix_mutex_init(&m);
    stub_pid = 1u;
    assert(rix_mutex_lock(&m) == 0);
    assert(rix_mutex_owner(&m, &owner) == 0 && owner == 1u);
    assert(rix_mutex_trylock(&m) == -2);
    stub_pid = 2u;
    assert(rix_mutex_trylock(&m) == 1);
    assert(rix_mutex_unlock(&m) == -3);
    stub_pid = 1u;
    assert(rix_mutex_unlock(&m) == 0);
    assert(rix_mutex_unlock(&m) == -3);
    uint64_t flags = 0;
    assert(rix_mutex_lock_irqsave(&m, &flags) == 0);
    assert(rix_mutex_unlock_irqrestore(&m, flags) == 0);
    assert(rix_mutex_lock(NULL) == -1);
    assert(rix_mutex_trylock(NULL) == -1);
    assert(rix_mutex_unlock(NULL) == -1);
    assert(rix_mutex_owner(NULL, &owner) == -1);
}

static void rwlock_checks(void) {
    rix_rwlock_t rw;
    rix_rwlock_init(&rw);
    stub_pid = 1u;
    assert(rix_rwlock_read_lock(&rw) == 0);
    stub_pid = 2u;
    assert(rix_rwlock_read_lock(&rw) == 0);
    stub_pid = 3u;
    assert(rix_rwlock_write_trylock(&rw) == 1);
    stub_pid = 1u;
    assert(rix_rwlock_read_unlock(&rw) == 0);
    stub_pid = 2u;
    assert(rix_rwlock_read_unlock(&rw) == 0);
    assert(rix_rwlock_read_unlock(&rw) == -3);
    stub_pid = 3u;
    assert(rix_rwlock_write_lock(&rw) == 0);
    stub_pid = 1u;
    assert(rix_rwlock_read_trylock(&rw) == 1);
    stub_pid = 3u;
    assert(rix_rwlock_write_trylock(&rw) == -2);
    assert(rix_rwlock_write_unlock(&rw) == 0);
    stub_pid = 1u;
    assert(rix_rwlock_write_unlock(&rw) == -3);
    assert(rix_rwlock_read_lock(&rw) == 0);
    assert(rix_rwlock_read_unlock(&rw) == 0);
    assert(rix_rwlock_read_lock(NULL) == -1);
    assert(rix_rwlock_write_lock(NULL) == -1);
}

static void sem_checks(void) {
    rix_sem_t s;
    uint32_t n = 0;
    rix_sem_init(&s, 2u, 3u);
    assert(rix_sem_down(&s) == 0);
    assert(rix_sem_down(&s) == 0);
    assert(rix_sem_trydown(&s) == 1);
    assert(rix_sem_up(&s) == 0);
    assert(rix_sem_count(&s, &n) == 0 && n == 1u);
    assert(rix_sem_up(&s) == 0);
    assert(rix_sem_up(&s) == 0);
    assert(rix_sem_up(&s) == -2);
    assert(rix_sem_down_timeout(&s, 0u) == 0);
    assert(rix_sem_down_timeout(&s, 0u) == 0);
    assert(rix_sem_down_timeout(&s, 0u) == 0);
    assert(rix_sem_down_timeout(&s, 0u) == 1);
    assert(rix_sem_down(NULL) == -1);
    assert(rix_sem_trydown(NULL) == -1);
    assert(rix_sem_up(NULL) == -1);
}

static void refcount_checks(void) {
    rix_refcount_t r;
    rix_ref_init(&r);
    assert(rix_ref_acquire(&r) == 0);
    assert(rix_ref_release(&r) == 0);
    assert(rix_ref_release(&r) == 1);
    assert(rix_ref_acquire(&r) == -1);
    assert(rix_ref_release(&r) == -1);
    assert(rix_ref_acquire(NULL) == -1);
    assert(rix_ref_release(NULL) == -1);
    rix_ref_init(NULL);
}

static void waitqueue_checks(void) {
    rix_waitqueue_t q;
    rix_wait_handle_t ha, hb, hc;
    rix_waitqueue_init(&q);
    assert(rix_waitqueue_prepare(&q, 11u, &ha) == 0);
    assert(rix_waitqueue_prepare(&q, 22u, &hb) == 0);
    assert(rix_waitqueue_block(&q, ha) == 0);
    assert(rix_waitqueue_block(&q, hb) == 0);
    assert(rix_waitqueue_take_wakeup(&q, ha) == 1);
    assert(rix_waitqueue_wake_one(&q) == 0);
    assert(rix_waitqueue_take_wakeup(&q, ha) == 0);
    assert(rix_waitqueue_take_wakeup(&q, ha) == 1);
    assert(rix_waitqueue_remove(&q, ha) == 0);
    assert(rix_waitqueue_take_wakeup(&q, ha) == -1);
    assert(rix_waitqueue_prepare(&q, 33u, &hc) == 0);
    assert(hc.generation != ha.generation);
    assert(rix_waitqueue_block(&q, ha) == -1);
    assert(rix_waitqueue_block(&q, hc) == 0);
    assert(rix_waitqueue_wake_all(&q) == 2);
    assert(rix_waitqueue_take_wakeup(&q, hb) == 0);
    assert(rix_waitqueue_take_wakeup(&q, hc) == 0);
    assert(rix_waitqueue_remove(&q, hb) == 0);
    assert(rix_waitqueue_remove(&q, hc) == 0);
    assert(rix_waitqueue_remove(&q, hc) == -1);
    rix_wait_handle_t tmp;
    for (unsigned i = 0; i < RIX_WQ_MAX_WAITERS; ++i)
        assert(rix_waitqueue_prepare(&q, 100u + i, &tmp) == 0);
    assert(rix_waitqueue_prepare(&q, 999u, &tmp) == -1);
    assert(rix_waitqueue_prepare(NULL, 1u, &tmp) == -1);
    assert(rix_waitqueue_block(NULL, tmp) == -1);
    assert(rix_waitqueue_wake_one(NULL) == -1);
    assert(rix_waitqueue_wake_all(NULL) == -1);
    assert(rix_waitqueue_remove(NULL, tmp) == -1);
}

static void lockdep_checks(void) {
    unsigned a = 0, b = 0, c = 0, bad = 0;
    assert(rix_lockdep_register("test-a", 10u, &a) == 0);
    assert(rix_lockdep_register("test-b", 20u, &b) == 0);
    assert(rix_lockdep_register("test-c", 15u, &c) == 0);
    assert(a != 0 && b != 0 && c != 0 && a != b && b != c);
    assert(rix_lockdep_register(NULL, 1u, &bad) == -1);
    assert(rix_lockdep_acquire(a) == 0);
    assert(rix_lockdep_acquire(b) == 0);
    assert(rix_lockdep_release(b) == 0);
    assert(rix_lockdep_acquire(c) == 0);
    assert(rix_lockdep_release(c) == 0);
    assert(rix_lockdep_release(a) == 0);
    unsigned before = rix_lockdep_violations();
    unsigned serial_before = stub_serial_calls;
    assert(rix_lockdep_acquire(b) == 0);
    assert(rix_lockdep_acquire(a) == -2);
    assert(rix_lockdep_violations() == before + 1u);
    assert(stub_serial_calls > serial_before);
    assert(rix_lockdep_release(b) == 0);
    assert(rix_lockdep_release(a) == -2);
    assert(rix_lockdep_violations() == before + 2u);
    assert(rix_lockdep_acquire(a) == 0);
    assert(rix_lockdep_acquire(a) == -2);
    assert(rix_lockdep_release(a) == 0);
    assert(rix_lockdep_acquire(0u) == 0);
    assert(rix_lockdep_release(0u) == 0);
    assert(rix_lockdep_acquire(99u) == -1);
    assert(rix_lockdep_release(99u) == -1);
}

int main(void) {
    mutex_checks();
    rwlock_checks();
    sem_checks();
    refcount_checks();
    waitqueue_checks();
    lockdep_checks();
    return 0;
}
