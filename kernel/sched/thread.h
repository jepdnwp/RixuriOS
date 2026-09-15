#pragma once
/* Phase R1 (Phase 06: PID/TID lifecycle): kernel thread objects.
 *
 * Every scheduler task slot (except the BSP boot slot, which owns the
 * reserved TID 1) is bound 1:1 to a thread object. A thread names its
 * owner process; several threads may share one owner (shared address
 * space — the object model Phase 25 clone() will build on; no syscall
 * wires it yet).
 *
 * Lifetime rule (refcounted ownership, enforced by the callers):
 *  - thread_alloc binds a live (non-UNUSED, non-ZOMBIE) owner pid.
 *  - process_exit detaches (not frees) the owner's threads: the task
 *    slots keep running until they exit, then free their threads.
 *  - wait/reap destroys the address space only when no ACTIVE thread
 *    of that pid remains; DETACHED threads keep the pre-R1 semantics
 *    (they die at the next address-space activation, which refuses
 *    ZOMBIE roots).
 * TIDs are monotonic and never reused (no ABA on stale references).
 *
 * Locking: the table is guarded by the scheduler lock (sched_lock in
 * scheduler.c). Every op below assumes it is held — hence the _locked
 * suffix. The two cross-module callers (process exit detach, reap
 * gating) go through scheduler.c wrappers that take sched_lock, the
 * same PROC4->sched edge process_exit_locked already uses for
 * scheduler_wake_pid. No new lock, no new lockdep edge. */
#include <stddef.h>
#include <stdint.h>

typedef uint64_t rix_tid_t;

#define RIX_THREAD_MAX 64u

typedef enum {
    RIX_THREAD_UNUSED = 0,
    RIX_THREAD_ACTIVE = 1,
    RIX_THREAD_DETACHED = 2
} rix_thread_state_t;

/* ABI snapshot shared with userspace (mirrored in user/libc/include/
 * unistd.h as rix_thread_info_t; keep the layout identical: 24 bytes,
 * 8-byte aligned). owner_pid is a snapshot — the owner may have been
 * reaped already; test liveness with process_lookup, not this field. */
typedef struct {
    uint64_t tid;
    uint64_t owner_pid;
    uint32_t state;
    uint32_t flags;
} rix_thread_info_t;

void thread_table_init(void);
/* Bind a new thread to a live owner. 0 ok, -1 dead/unknown owner,
 * -2 table full. */
int thread_alloc_locked(uint64_t owner_pid, rix_tid_t *out_tid);
void thread_free_locked(rix_tid_t tid);
/* Mark every ACTIVE thread of pid DETACHED (process exit path). */
void thread_detach_pid_locked(uint64_t pid);
/* Count live (ACTIVE, not DETACHED) threads of pid (reap gate). */
size_t thread_count_locked(uint64_t pid);
/* Snapshot one thread. 0 ok, -1 unknown/dead tid. */
int thread_lookup_locked(rix_tid_t tid, rix_thread_info_t *out);
size_t thread_live_count_locked(void);
/* Fill a caller snapshot array. 0 ok, -1 capacity too small. */
int thread_list_locked(rix_thread_info_t *out, size_t capacity, size_t *count);
