/* Phase R1 host test: thread-object lifecycle (kernel/sched/thread.c).
 * Stubs process_lookup with a controllable fake table; exercises alloc
 * validation, monotonic non-reusing TIDs, detach, reap-gate counting,
 * lookup, list overflow, and table exhaustion. */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "kernel/sched/thread.h"
#include "kernel/process/process.h"

/* Fake owner table: pid -> state. process_lookup scans it. */
static rix_process_t fake_procs[8];
static size_t fake_count;

rix_process_t *process_lookup(pid_t pid) {
    for (size_t i = 0; i < fake_count; i++)
        if (fake_procs[i].state != RIX_PROC_UNUSED && fake_procs[i].pid == pid)
            return &fake_procs[i];
    return 0;
}

static void fake_add(pid_t pid, rix_process_state_t state) {
    assert(fake_count < 8);
    fake_procs[fake_count].pid = pid;
    fake_procs[fake_count].state = state;
    fake_count++;
}

static void fake_set(pid_t pid, rix_process_state_t state) {
    for (size_t i = 0; i < fake_count; i++)
        if (fake_procs[i].pid == pid) fake_procs[i].state = state;
}

int main(void) {
    rix_tid_t tid = 0, first = 0;
    rix_thread_info_t info;

    thread_table_init();
    assert(thread_live_count_locked() == 0);

    fake_add(1, RIX_PROC_RUNNING);
    fake_add(2, RIX_PROC_SLEEPING);
    fake_add(3, RIX_PROC_ZOMBIE);

    /* pid 0 (kernel) always binds. */
    assert(thread_alloc_locked(0, &tid) == 0);
    assert(tid == 1);
    first = tid;
    /* Live owners bind regardless of RUNNING vs SLEEPING. */
    assert(thread_alloc_locked(1, &tid) == 0);
    assert(tid == first + 1);
    assert(thread_alloc_locked(2, &tid) == 0);
    /* Dead owners refuse: unknown pid, UNUSED slot, ZOMBIE. */
    assert(thread_alloc_locked(99, &tid) == -1);
    fake_add(4, RIX_PROC_UNUSED);
    assert(thread_alloc_locked(4, &tid) == -1);
    assert(thread_alloc_locked(3, &tid) == -1);
    assert(thread_alloc_locked(0, 0) == -1);
    assert(thread_live_count_locked() == 3);

    /* Lookup + list snapshot. */
    assert(thread_lookup_locked(first, &info) == 0);
    assert(info.tid == first && info.owner_pid == 0 && info.state == RIX_THREAD_ACTIVE);
    assert(thread_lookup_locked(0, &info) == -1);
    assert(thread_lookup_locked(9999, &info) == -1);
    assert(thread_lookup_locked(first, 0) == -1);
    {
        rix_thread_info_t snap[8];
        size_t count = 0;
        assert(thread_list_locked(snap, 8, &count) == 0 && count == 3);
        assert(thread_list_locked(snap, 2, &count) == -1);
        assert(thread_list_locked(0, 8, &count) == -1);
        assert(thread_list_locked(snap, 8, 0) == -1);
    }

    /* Reap-gate counting: ACTIVE only. */
    assert(thread_count_locked(1) == 1);
    assert(thread_count_locked(2) == 1);
    assert(thread_count_locked(3) == 0);

    /* Exit detaches (not frees): count drops, entry lingers DETACHED. */
    thread_detach_pid_locked(1);
    assert(thread_count_locked(1) == 0);
    assert(thread_live_count_locked() == 3);
    assert(thread_lookup_locked(first + 1, &info) == 0);
    assert(info.state == RIX_THREAD_DETACHED && info.owner_pid == 1);

    /* Free is idempotent-safe; TIDs never reuse (no ABA). */
    thread_free_locked(first + 1);
    thread_free_locked(first + 1);
    assert(thread_live_count_locked() == 2);
    assert(thread_alloc_locked(2, &tid) == 0);
    assert(tid > first + 2);

    /* Owner dying between alloc and use: alloc refuses ZOMBIE. */
    fake_set(2, RIX_PROC_ZOMBIE);
    assert(thread_alloc_locked(2, &tid) == -1);
    fake_set(2, RIX_PROC_RUNNING);

    /* Exhaustion: fill to RIX_THREAD_MAX, next alloc fails -2, and a
     * free slot recovers. */
    {
        size_t live = thread_live_count_locked();
        rix_tid_t filler;
        for (size_t i = live; i < RIX_THREAD_MAX; i++)
            assert(thread_alloc_locked(2, &filler) == 0);
        assert(thread_live_count_locked() == RIX_THREAD_MAX);
        assert(thread_alloc_locked(2, &filler) == -2);
        thread_free_locked(filler - 1);
        assert(thread_alloc_locked(0, &filler) == 0);
    }

    printf("thread_test: PASS\n");
    return 0;
}
