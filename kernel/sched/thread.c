#include "thread.h"
#include "../process/process.h"

/* Pure table logic: no locking inside (caller holds sched_lock).
 * Host-testable: the only external dependency is process_lookup plus
 * the RIX_PROC_* state constants, both stubbed by tests/thread_test.c. */

typedef struct {
    rix_tid_t tid;
    uint64_t owner_pid;
    uint8_t state;
} rix_thread_t;

static rix_thread_t threads[RIX_THREAD_MAX];
static rix_tid_t next_tid;

void thread_table_init(void) {
    for (size_t i = 0; i < RIX_THREAD_MAX; i++) {
        threads[i].tid = 0;
        threads[i].owner_pid = 0;
        threads[i].state = RIX_THREAD_UNUSED;
    }
    next_tid = 1;
}

static rix_thread_t *find_locked(rix_tid_t tid) {
    if (!tid) return 0;
    for (size_t i = 0; i < RIX_THREAD_MAX; i++)
        if (threads[i].state != RIX_THREAD_UNUSED && threads[i].tid == tid)
            return &threads[i];
    return 0;
}

/* Owner liveness is a lock-free single-word read (process_lookup takes
 * no locks by design). Race vs concurrent process_exit: exit marks
 * ZOMBIE under PROCESS_GUARD and detaches under sched_lock, while this
 * runs under sched_lock. Either the ZOMBIE mark lands first (we see it
 * and refuse) or the insert lands first (exit's detach then marks the
 * new thread DETACHED). In the latter case the new task's bootstrap
 * refuses the ZOMBIE root via process_activate/validate and dies via
 * task_returned, freeing the thread — bounded, no corruption, no leak. */
static int owner_live(uint64_t pid) {
    if (pid == 0) return 1;
    rix_process_t *p = process_lookup((pid_t)pid);
    if (!p) return 0;
    if (p->state == RIX_PROC_UNUSED || p->state == RIX_PROC_ZOMBIE) return 0;
    return 1;
}

int thread_alloc_locked(uint64_t owner_pid, rix_tid_t *out_tid) {
    if (!out_tid) return -1;
    if (!owner_live(owner_pid)) return -1;
    for (size_t i = 0; i < RIX_THREAD_MAX; i++) {
        if (threads[i].state != RIX_THREAD_UNUSED) continue;
        rix_tid_t tid = next_tid++;
        if (!tid) tid = next_tid++; /* skip 0 on (impossible) wrap */
        threads[i].tid = tid;
        threads[i].owner_pid = owner_pid;
        threads[i].state = RIX_THREAD_ACTIVE;
        *out_tid = tid;
        return 0;
    }
    return -2;
}

void thread_free_locked(rix_tid_t tid) {
    rix_thread_t *t = find_locked(tid);
    if (!t) return;
    t->tid = 0;
    t->owner_pid = 0;
    t->state = RIX_THREAD_UNUSED;
}

void thread_detach_pid_locked(uint64_t pid) {
    for (size_t i = 0; i < RIX_THREAD_MAX; i++)
        if (threads[i].state == RIX_THREAD_ACTIVE && threads[i].owner_pid == pid)
            threads[i].state = RIX_THREAD_DETACHED;
}

size_t thread_count_locked(uint64_t pid) {
    size_t n = 0;
    for (size_t i = 0; i < RIX_THREAD_MAX; i++)
        if (threads[i].state == RIX_THREAD_ACTIVE && threads[i].owner_pid == pid)
            n++;
    return n;
}

int thread_lookup_locked(rix_tid_t tid, rix_thread_info_t *out) {
    if (!out) return -1;
    rix_thread_t *t = find_locked(tid);
    if (!t) return -1;
    out->tid = t->tid;
    out->owner_pid = t->owner_pid;
    out->state = (uint32_t)t->state;
    out->flags = 0;
    return 0;
}

size_t thread_live_count_locked(void) {
    size_t n = 0;
    for (size_t i = 0; i < RIX_THREAD_MAX; i++)
        if (threads[i].state != RIX_THREAD_UNUSED)
            n++;
    return n;
}

int thread_list_locked(rix_thread_info_t *out, size_t capacity, size_t *count) {
    size_t total = 0, index = 0;
    if (!count || (!out && capacity)) return -1;
    for (size_t i = 0; i < RIX_THREAD_MAX; i++)
        if (threads[i].state != RIX_THREAD_UNUSED) total++;
    if (capacity < total) return -1;
    for (size_t i = 0; i < RIX_THREAD_MAX; i++) {
        if (threads[i].state == RIX_THREAD_UNUSED) continue;
        out[index].tid = threads[i].tid;
        out[index].owner_pid = threads[i].owner_pid;
        out[index].state = (uint32_t)threads[i].state;
        out[index].flags = 0;
        index++;
    }
    *count = total;
    return 0;
}
