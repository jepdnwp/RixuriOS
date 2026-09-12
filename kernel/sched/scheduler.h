#pragma once
#include <stdint.h>

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
void scheduler_yield(void);
rix_task_id_t scheduler_current_id(void);
uint32_t scheduler_runnable_count(void);
/* Phase E2: mark a kernel-thread task AP-runnable (default BSP-only).
 * 0 ok, -1 unknown id. Tasks[0], user tasks and unaudited workers stay
 * BSP-pinned; APs run only ap_ok kernel threads. */
int scheduler_task_allow_ap(rix_task_id_t id);
/* Phase E2: AP idle entry (called once from ap_entry, never returns).
 * Captures the AP stack as the idle context, then hlt-parks, running
 * ap_ok kernel-thread tasks as they appear. */
__attribute__((noreturn)) void scheduler_ap_idle(void);
/* Read-only dump of all task slots (id/state/pid) for triage + Phase-34
 * evidence. No locks, no state change; safe pre/post CR3 switch. */
void scheduler_dump_states(void);
