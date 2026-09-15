#pragma once
#include <stddef.h>
#include <stdint.h>
#include "thread.h"
#include "../sync/waitqueue.h"

typedef uint64_t rix_task_id_t;
typedef void (*rix_kernel_thread_fn)(void *arg);
typedef struct {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t rip, rflags, rsp;
} rix_user_context_t;

int scheduler_init(void);
void scheduler_tick(void);
uint64_t scheduler_ticks(void);
int scheduler_create_kernel_thread(rix_kernel_thread_fn entry, void *arg, rix_task_id_t *out_id);
int scheduler_create_user_process(uint64_t pid, uint64_t entry, uint64_t user_stack, rix_task_id_t *out_id);
int scheduler_create_fork_child(uint64_t pid, uint64_t entry, uint64_t user_stack,
                                uint64_t return_value, rix_task_id_t *out_id);
int scheduler_create_fork_child_context(uint64_t pid, const rix_user_context_t *context,
                                        rix_task_id_t *out_id);
__attribute__((noreturn)) void scheduler_exit_current(void);
/* Phase P3-B slice 2: cooperative preempt-disable + quantum.
 * PIT only arms need-resched; switches happen in task context
 * (scheduler_yield) or in an IRQ-return path after EOI. Nesting
 * disable/enable, IRQ-safe, never sleeps. */
void scheduler_preempt_disable(void);
void scheduler_preempt_enable(void);
int scheduler_preempt_is_disabled(void);
int scheduler_need_resched(void);
void scheduler_clear_need_resched(void);
void scheduler_preempt_tick(void);
int scheduler_should_yield_from_irq(void);
void scheduler_yield(void);
rix_task_id_t scheduler_current_id(void);
uint32_t scheduler_runnable_count(void);
/* Phase P3 backend slice 1: true task blocking on waitqueues.
 * A task prepared on a queue, marked BLOCKED, is skipped by selection
 * until a waker marks it RUNNABLE again; waiter lifetime is bound to
 * the task (exit paths drop it). Spurious wakeups are safe: takers
 * always re-check their condition. Protocol per episode: prepare ->
 * block -> [woken] -> take -> done(remove). */
int scheduler_wait_prepare(rix_waitqueue_t *wq, rix_wait_handle_t *out);
void scheduler_block_current(void);
int scheduler_wait_take(void);
void scheduler_wait_abort(void);
void scheduler_wake_queue(rix_waitqueue_t *wq);
/* Wake tasks of one process (signal delivery): any BLOCKED task bound
 * to pid becomes RUNNABLE so it re-checks (EINTR or spurious re-block
 * are both correct outcomes). Spurious wakes are safe by the
 * take-then-recheck protocol. */
void scheduler_wake_pid(uint64_t pid);/* Phase E2: mark a kernel-thread task AP-runnable (default BSP-only).
 * 0 ok, -1 unknown id. Tasks[0], user tasks and unaudited workers stay
 * BSP-pinned; APs run only ap_ok kernel threads. */
int scheduler_task_allow_ap(rix_task_id_t id);
/* Phase R3: pin a task to a CPU subset (bit c = may run on CPU c).
 * Narrowing only within the BASE rule (BSP bit always; AP bits only
 * for ap_ok kernel threads): stranding masks are refused, so pinning
 * can never silence a task. 0 ok, -1 unknown id / empty / stranded. */
int scheduler_set_affinity(rix_task_id_t id, uint64_t mask);
/* Phase R3: cross-CPU claim count of the calling task (migration
 * accounting; lock-free self-read, see scheduler.c). */
uint32_t scheduler_current_migrations(void);
/* Phase R4: priority levels (kernel threads only; user tasks stay
 * NORMAL) + run-tick self-read. High preference with streak-cap
 * starvation guard lives in runqueue.h (RIX_RQ_HIPRI_CAP). */
#define RIX_PRIO_NORMAL 0u
#define RIX_PRIO_HIGH 1u
#define RIX_PRIO_MAX 1u
int scheduler_set_priority(rix_task_id_t id, unsigned level);
uint64_t scheduler_current_run_ticks(void);
/* Phase E2: AP idle entry (called once from ap_entry, never returns).
 * Captures the AP stack as the idle context, then hlt-parks, running
 * ap_ok kernel-thread tasks as they appear. */
__attribute__((noreturn)) void scheduler_ap_idle(void);
/* Read-only dump of all task slots (id/tid/state/pid) for triage +
 * Phase-34 evidence. No locks, no state change; safe pre/post CR3
 * switch. */
void scheduler_dump_states(void);
/* Phase R1 (Phase 06: PID/TID lifecycle). Thread objects live in
 * thread.[hc] under sched_lock; these wrappers take the lock (safe
 * under PROCESS_GUARD — same PROC4->sched edge as wake_pid). */
size_t scheduler_thread_count_for_pid(uint64_t pid);
void scheduler_thread_detach_pid(uint64_t pid);
rix_tid_t scheduler_current_tid(void);
int scheduler_list_threads(rix_thread_info_t *out, size_t capacity, size_t *count);
