#include "scheduler.h"
#include "thread.h"
#include "runqueue.h"
#include "../arch/x86_64/irq.h"
#include "../arch/x86_64/smp.h"
#include "../sync/lock.h"
#include "../process/process.h"
#include "../arch/x86_64/user_entry.h"
#include "kernel.h"
#include <stddef.h>
#include <stdint.h>

#define RIX_MAX_TASKS 32
#define RIX_STACK_SIZE 16384

typedef enum { TASK_UNUSED=0, TASK_RUNNABLE=1, TASK_RUNNING=2, TASK_DEAD=3, TASK_BLOCKED=4 } task_state_t;
typedef struct {
    rix_task_id_t id;
    task_state_t state;
    /* Phase R1: bound thread object (0 = none; only tasks[0] starts
     * without one until scheduler_init reserves TID 1). Freed exactly
     * once, at task death, via task_mark_dead_locked. */
    rix_tid_t tid;
    uint64_t rsp;
    rix_kernel_thread_fn entry;
    void *arg;
    uint64_t process_pid;
    uint64_t user_entry;
    uint64_t user_stack;
    uint64_t user_return;
    uint8_t user_context_valid;
    /* Phase E2: AP-runnable affinity. 0 = BSP-only (default: today's
     * placement for every task); 1 = APs may run it (kernel threads
     * only — user tasks and tasks[0] never migrate). Workers flip to 1
     * one by one with per-driver concurrency audits, never silently. */
    uint8_t ap_ok;
    /* Phase R3: placement mask (bit c = may run on CPU c, up to 64).
     * Defaults to all-CPUs; the BASE rule (BSP: any, AP: ap_ok kernel
     * threads, never slot 0) stays the hard gate, so the default
     * changes nothing until scheduler_set_affinity narrows a task.
     * rq_eligible intersects both; the masks cache the intersection. */
    uint64_t affinity;
    /* Phase R3 migration accounting: last CPU that claimed this slot
     * (~0u = never ran) + cross-CPU claim count. Written only at claim
     * sites under sched_lock; a RUNNING task's own row is stable, so
     * the owner reads it lock-free. */
    uint32_t run_cpu;
    uint32_t migrations;
    /* Phase R4: priority (0 normal, 1 high; kernel threads only — user
     * tasks stay 0) + quantum accounting. run_ticks accumulates whole
     * RUNNING intervals [claim-stamp, yield-old/block/death] in PIT
     * ticks; voluntary/involuntary counts genuine yields from RUNNING
     * (armed-flag consumed = preempted). Owner self-reads lock-free. */
    uint8_t prio;
    uint64_t run_ticks;
    uint64_t stamp;
    uint32_t voluntary;
    uint32_t involuntary;
    rix_user_context_t user_context;
    uint8_t stack[RIX_STACK_SIZE] __attribute__((aligned(16)));
    /* Phase P3 backend: wait binding. Set by scheduler_wait_prepare,
     * cleared by take/done/abort or by the exit paths. A non-NULL wq
     * always names a live waiter owned by this task (exit paths drop
     * it, so slots never leak on blocked-task exit). */
    rix_waitqueue_t *wait_wq;
    rix_wait_handle_t wait_handle;
    /* Phase P3-B slice 2: per-task FPU image (fxsave area, 512 B).
     * rix_context_switch only preserves integer callee-saved state;
     * with involuntary preemption a tick can land mid-SSE-sequence in
     * kernel code, so every stack switch must carry the FPU image too.
     * Fresh tasks start from the fninit template (see task_init_stack);
     * user FPU inheritance across fork/exec stays explicitly out of
     * scope until P25 threads (documented boundary). */
    uint8_t fpu[512] __attribute__((aligned(16)));
} rix_task_t;

/* user_entry.S consumes rix_user_context_t at fixed byte offsets.  Keep the
 * C/assembly ABI explicit so a field change cannot silently corrupt IRETQ. */
_Static_assert(offsetof(rix_user_context_t, r15) == 0, "user context r15 offset");
_Static_assert(offsetof(rix_user_context_t, r8) == 56, "user context r8 offset");
_Static_assert(offsetof(rix_user_context_t, rsi) == 80, "user context rsi offset");
_Static_assert(offsetof(rix_user_context_t, rax) == 112, "user context rax offset");
_Static_assert(offsetof(rix_user_context_t, rip) == 120, "user context rip offset");
_Static_assert(offsetof(rix_user_context_t, rflags) == 128, "user context rflags offset");
_Static_assert(offsetof(rix_user_context_t, rsp) == 136, "user context rsp offset");
_Static_assert(sizeof(rix_user_context_t) == 144, "user context size");

extern void rix_context_switch(uint64_t *old_rsp,uint64_t new_rsp);
static volatile uint64_t ticks;
static rix_task_t tasks[RIX_MAX_TASKS];
/* Phase E1: per-CPU current-task slot + one scheduler spinlock.
 * Zero-init is correct: every CPU conceptually starts running tasks[0].
 * Lock rule: irqsave in create/exit/returned paths (arbitrary caller IRQ
 * posture); plain lock/unlock in yield (IRQs already off via cli).
 * NEVER held across rix_context_switch or process_activate. */
static uint32_t cpu_current[SMP_MAX_CPUS];
static rix_spinlock_t sched_lock;
static rix_task_id_t next_id;
/* Phase R2: per-CPU runqueues. cpu_rq[c].mask caches the RUNNABLE +
 * eligible set for CPU c (BSP: any RUNNABLE; AP: RUNNABLE kernel-thread
 * ap_ok, never slot 0); the cursor gives per-CPU round-robin instead of
 * the old global scan's head-of-line. Every state transition maintains
 * the masks (see rq_add_eligible/remove_all call sites); rq_verify
 * fail-stops on any drift. All accesses under sched_lock. */
static rix_runqueue_t cpu_rq[SMP_MAX_CPUS];
_Static_assert(RIX_MAX_TASKS==RIX_RQ_SLOTS,"runqueue slots cover tasks");
/* Phase E2 AP idle slots: captured AP stack (switch target when no task
 * is runnable) + validity. The BSP never sets its slot. cpu_idle_tmp is
 * the dummy save area for the yield-to-idle switch: the dead task's own
 * slot must NOT be written after unlock (another CPU may already have
 * recycled it for a new task). */
static uint64_t cpu_idle_rsp[SMP_MAX_CPUS];
static uint8_t cpu_idle_valid[SMP_MAX_CPUS];
static uint64_t cpu_idle_tmp[SMP_MAX_CPUS];
/* Per-CPU FPU image for the idle context (which owns no task slot).
 * Rows are 512 B (multiple of 16), base is 16-aligned, so every row
 * satisfies the fxsave/fxrstor alignment requirement. */
static uint8_t cpu_idle_fpu[SMP_MAX_CPUS][512] __attribute__((aligned(16)));
static uint8_t fpu_template[512] __attribute__((aligned(16)));
static inline void fpu_switch(const void *old_area, const void *new_area) {
    __asm__ volatile("fxsave (%0)" :: "r"(old_area) : "memory");
    __asm__ volatile("fxrstor (%0)" :: "r"(new_area) : "memory");
}
static uint32_t sched_cpu(void){
    int id=smp_cpu_id();
    if(id>=0&&id<SMP_MAX_CPUS)return (uint32_t)id;
    int b=smp_bsp_index();
    if(b>=0&&b<SMP_MAX_CPUS)return (uint32_t)b;
    return 0;
}
static void task_drop_waiter(rix_task_t *t);
/* Forward: single task-death choke point (defined below, used by the
 * early exit paths above). */
static void task_mark_dead_locked(rix_task_t *t);
/* Forward: Phase R2 mask-cache helpers (defined below with the rest of
 * the R2 block, used by allow_ap above) + cli (defined below, used by
 * the fail-stop verify). */
static void rq_add_eligible_locked(uint32_t i);
static void rq_remove_all_locked(uint32_t i);
static void rq_resync_locked(uint32_t i);
static void cli(void);
int scheduler_task_allow_ap(rix_task_id_t id){
    if(!id)return -1;
    uint64_t irq;rix_spin_lock_irqsave(&sched_lock,&irq);
    int rc=-1;
    for(uint32_t i=1;i<RIX_MAX_TASKS;i++){
        if(tasks[i].id==id&&tasks[i].state!=TASK_UNUSED){
            /* Kernel threads only: user tasks never migrate (checked
             * again at selection). tasks[0] excluded by the scan. */
            if(tasks[i].process_pid==0){
                tasks[i].ap_ok=1;
                /* Phase R2/R3: a RUNNABLE task whose eligibility just
                 * widened must re-sync its mask bits now (the old global
                 * scan read ap_ok live; the masks are a cache).
                 * RUNNING tasks re-add with fresh eligibility at their
                 * next yield-old; BLOCKED/DEAD tasks never hold bits. */
                rq_resync_locked(i);
                rc=0;
            }
            break;
        }
    }
    rix_spin_unlock_irqrestore(&sched_lock,irq);
    return rc;
}
/* Phase R3: narrow (or restore) a task's placement mask. The BASE rule
 * stays the hard gate: bits outside it (AP bits for non-ap_ok/user
 * tasks, slot-0-on-AP) grant nothing, and a mask intersecting the base
 * rule nowhere is refused (-1) so a task can never be pinned into
 * never-runnable silence. A RUNNING task on a now-excluded CPU keeps
 * running until its next yield (standard); its next pick obeys the new
 * mask. 0 ok, -1 unknown id / empty mask / stranded placement. */
int scheduler_set_affinity(rix_task_id_t id,uint64_t mask){
    uint64_t base_allowed;
    int b,rc=-1;
    uint64_t irq;
    if(!id||!mask)return -1;
    b=smp_bsp_index();
    if(b<0||b>=SMP_MAX_CPUS)return -1;
    rix_spin_lock_irqsave(&sched_lock,&irq);
    for(uint32_t i=1;i<RIX_MAX_TASKS;i++){
        if(tasks[i].id!=id||tasks[i].state==TASK_UNUSED)continue;
        base_allowed=(1ULL<<(uint64_t)b);
        if(tasks[i].process_pid==0&&tasks[i].ap_ok)base_allowed=~0ULL;
        if(!(mask&base_allowed))break;
        tasks[i].affinity=mask;
        rq_resync_locked(i);
        rc=0;
        break;
    }
    rix_spin_unlock_irqrestore(&sched_lock,irq);
    return rc;
}

/* Phase R2 selection. Eligibility mirrors the old global scan exactly
 * (BSP takes any RUNNABLE; APs take only RUNNABLE kernel-thread ap_ok
 * tasks, never slot 0) but reads the calling CPU's own mask + cursor,
 * so CPUs no longer scan past each other's ineligible tasks.
 * sched_lock must be held. Pure selection: the caller flips states and
 * maintains mask bits at the flip lines. Stale bits (impossible if all
 * transition sites are correct) are healed on the spot, bounded to one
 * full rotation; the cursor advances once, on success. */
static int rq_cpu_is_bsp(uint32_t c){return (int)c==smp_bsp_index();}
static int rq_eligible(uint32_t c,uint32_t i){
    if(i>=RIX_MAX_TASKS||c>=(uint32_t)SMP_MAX_CPUS)return 0;
    if(!(tasks[i].affinity&(1ULL<<(uint64_t)c)))return 0;
    if(rq_cpu_is_bsp(c))return 1;
    return i!=0&&tasks[i].process_pid==0&&tasks[i].ap_ok;
}
static void rq_add_eligible_locked(uint32_t i){
    for(uint32_t c=0;c<(uint32_t)SMP_MAX_CPUS;c++)
        if(rq_eligible(c,i))rq_add_locked(&cpu_rq[c],i);
}
static void rq_remove_all_locked(uint32_t i){
    for(uint32_t c=0;c<(uint32_t)SMP_MAX_CPUS;c++)rq_remove_locked(&cpu_rq[c],i);
}
/* Phase R3: re-sync one slot's bits after an eligibility change
 * (ap_ok flip, affinity narrowing): drop everywhere, re-add where the
 * new rule admits it. RUNNING/BLOCKED/DEAD slots hold no bits, so the
 * re-add is conditional on RUNNABLE. */
static void rq_resync_locked(uint32_t i){
    rq_remove_all_locked(i);
    if(i<RIX_MAX_TASKS&&tasks[i].state==TASK_RUNNABLE)rq_add_eligible_locked(i);
}
static uint32_t sched_pick_locked(uint32_t me,uint32_t old){
    /* Phase R4: high-priority subset for this pick (bit i = prio>0).
     * Built live every pick — no mask coupling (prio never moves bits;
     * rq_eligible/verify are prio-blind by design). */
    uint32_t himask=0;
    if(me>=(uint32_t)SMP_MAX_CPUS)return old;
    for(uint32_t i=0;i<RIX_MAX_TASKS;i++)if(tasks[i].prio)himask|=(1u<<i);
    for(uint32_t n=0;n<RIX_MAX_TASKS;n++){
        int pi=rq_pick_locked(&cpu_rq[me],himask);
        if(pi<0)break;
        uint32_t i=(uint32_t)pi;
        if(tasks[i].state==TASK_RUNNABLE&&rq_eligible(me,i))return i;
        rq_remove_locked(&cpu_rq[me],i);
    }
    /* Phase R3 work conservation: an AP whose own mask is empty steals
     * AP-eligible work from the BSP mask instead of parking while
     * runnable work waits. The thief's cursor/streak rotate over the
     * victim mask via a temporary (written back on success only); the
     * claim path (remove_all at the RUNNING flip) keeps every mask
     * consistent; eligibility is re-checked for the thief, so
     * BSP-pinned tasks are never stolen. BSP never falls back (its mask
     * already holds every RUNNABLE). Stale snapshot bits are dropped
     * from the temporary (the BSP mask owner heals its own; death and
     * block already remove_all, so staleness is impossible — the bound
     * is defense in depth). */
    if(!rq_cpu_is_bsp(me)){
        uint32_t b=(uint32_t)smp_bsp_index();
        if(b<(uint32_t)SMP_MAX_CPUS){
            rix_runqueue_t tmp;
            tmp.mask=cpu_rq[b].mask;tmp.cursor=cpu_rq[me].cursor;tmp.hipri_streak=cpu_rq[me].hipri_streak;
            for(uint32_t n=0;n<RIX_MAX_TASKS;n++){
                int pi=rq_pick_locked(&tmp,himask);
                if(pi<0)break;
                uint32_t i=(uint32_t)pi;
                if(tasks[i].state==TASK_RUNNABLE&&rq_eligible(me,i)){
                    cpu_rq[me].cursor=tmp.cursor;cpu_rq[me].hipri_streak=tmp.hipri_streak;
                    return i;
                }
                tmp.mask&=~(1u<<i);
            }
        }
    }
    return old;
}
/* Phase R3 migration accounting (sched_lock held at both claim sites:
 * scheduler_yield and scheduler_ap_idle). First claim sets run_cpu;
 * later claims from another CPU bump migrations. */
static void rq_note_claim_locked(uint32_t me,uint32_t next){
    if(next>=RIX_MAX_TASKS||me>=(uint32_t)SMP_MAX_CPUS)return;
    if(tasks[next].run_cpu!=~0u&&tasks[next].run_cpu!=me)tasks[next].migrations++;
    tasks[next].run_cpu=me;
}
/* Lock-free self-read: the caller's own slot is RUNNING, hence holds
 * no mask bits and cannot be claimed elsewhere — its row is stable. */
uint32_t scheduler_current_migrations(void){
    uint32_t me=sched_cpu();
    if(me>=SMP_MAX_CPUS)return 0;
    return tasks[cpu_current[me]].migrations;
}
/* Phase R4 self-reads (same stability argument as migrations). */
uint64_t scheduler_current_run_ticks(void){
    uint32_t me=sched_cpu();
    if(me>=SMP_MAX_CPUS)return 0;
    return tasks[cpu_current[me]].run_ticks;
}
/* Phase R4: boost a kernel thread to HIGH priority (audited scope,
 * mirroring allow_ap's kernel-only rule — user tasks stay NORMAL so
 * userspace keeps round-robin fairness). Prio never moves mask bits
 * (picks read it live), so no resync is needed on any state. 0 ok,
 * -1 unknown id / bad level / user task boost. */
int scheduler_set_priority(rix_task_id_t id,unsigned level){
    int rc=-1;
    uint64_t irq;
    if(!id||level>RIX_PRIO_MAX)return -1;
    rix_spin_lock_irqsave(&sched_lock,&irq);
    for(uint32_t i=1;i<RIX_MAX_TASKS;i++){
        if(tasks[i].id!=id||tasks[i].state==TASK_UNUSED)continue;
        if(level>0&&tasks[i].process_pid!=0)break;
        tasks[i].prio=(uint8_t)level;
        rc=0;
        break;
    }
    rix_spin_unlock_irqrestore(&sched_lock,irq);
    return rc;
}
/* Phase R2 fail-stop invariant: bit i in mask c ⟺ tasks[i] RUNNABLE and
 * eligible on c. Runs at every pick (both yield and AP-idle paths):
 * O(CPUs×slots), a few hundred cycles against a context switch. Any
 * drift logs once and freezes (cli+hlt) — a missed transition site must
 * brick the boot loudly, never starve a task silently. */
static void rq_verify_locked(void){
    for(uint32_t c=0;c<(uint32_t)SMP_MAX_CPUS;c++){
        for(uint32_t i=0;i<RIX_MAX_TASKS;i++){
            int bit=rq_contains_locked(&cpu_rq[c],i);
            int want=(tasks[i].state==TASK_RUNNABLE&&rq_eligible(c,i));
            if(bit!=want){
                kernel_log("RQVERIFY drift cpu=");kernel_log_dec(c);
                kernel_log(" idx=");kernel_log_dec(i);
                kernel_log(" bit=");kernel_log_dec((uint64_t)bit);
                kernel_log(" want=");kernel_log_dec((uint64_t)want);
                kernel_log(" st=");kernel_log_dec((uint64_t)tasks[i].state);
                kernel_log("\r\n");
                for(;;){cli();__asm__ volatile("hlt" ::: "memory");}
            }
        }
    }
}

static uint64_t read_rflags(void){uint64_t v;__asm__ volatile("pushfq; popq %0":"=r"(v)::"memory");return v;}
static void cli(void){__asm__ volatile("cli" ::: "memory");}
static void sti(void){__asm__ volatile("sti" ::: "memory");}

/* Phase P3-B slice 2: cooperative preempt-disable + tick quantum.
 * PIT (100 Hz) only ARMS a per-CPU need-resched flag; the actual
 * context switch happens in scheduler_yield() task context or in an
 * IRQ-return path after EOI. This keeps PIT-safe the P1-failure
 * class (IRQ preemption landing mid-allocator: pmm/heap/vmm/process
 * critical sections are all locked or IF=0, never async-switched).
 * Depth is nesting (irqsave-style): scheduler_preempt_disable() /
 * scheduler_preempt_enable() pair; scheduler_preempt_is_disabled()
 * reports the local CPU state (no locks, IRQ-safe read). */

#define RIX_PREEMPT_QUANTUM_TICKS 10u /* 100 Hz PIT -> 100 ms quantum */

static volatile unsigned cpu_preempt_depth[SMP_MAX_CPUS];
static volatile unsigned cpu_need_resched[SMP_MAX_CPUS];
static volatile uint64_t cpu_quantum_left[SMP_MAX_CPUS];
/* Phase E7: AP hlt-park flag (own-CPU writes). scheduler_ap_idle sets it
 * around hlt; the IRQ/IPI-return yield gate reads it. Without this, an
 * IPI landing on a hlt-parked AP would yield with a stale cpu_current
 * slot (whatever ran last, possibly recycled by another CPU) and corrupt
 * scheduler state. A missed pickup from the gate's races only delays an
 * AP (the BSP backstops every ap_ok task), never hangs. */
static volatile uint8_t cpu_in_idle[SMP_MAX_CPUS];
static __attribute__((noreturn)) void task_returned(void){ uint64_t irq;rix_spin_lock_irqsave(&sched_lock,&irq);task_mark_dead_locked(&tasks[cpu_current[sched_cpu()]]);rix_spin_unlock_irqrestore(&sched_lock,irq);for(;;) scheduler_yield(); }
static void boot_user_entry_marker(void){serial_write("BOOT: USER ENTRY READY\r\n");}
static void trace_yield_begin(void){}
static void trace_flags(void){}
static void trace_searching(void){}
static void trace_old_next(uint32_t old,uint32_t next){(void)old;(void)next;}
static void trace_old_updated(uint32_t old){(void)old;}
static void trace_activating(uint64_t pid){(void)pid;}
/* First user task selected: full transition inputs in one bounded block. */
static void trace_first_task(uint32_t idx){static unsigned n=0;if(n<1&&tasks[idx].process_pid){kernel_log("DEBUG: first task id=");kernel_log_dec(tasks[idx].id);kernel_log(" state=");kernel_log_dec(tasks[idx].state);kernel_log(" pid=");kernel_log_dec(tasks[idx].process_pid);kernel_log(" rsp=");kernel_log_hex(tasks[idx].rsp);kernel_log(" entry=");kernel_log_hex(tasks[idx].user_entry);kernel_log(" user_stack=");kernel_log_hex(tasks[idx].user_stack);kernel_log("\r\n");n++;}}
static void trace_selected(uint64_t id,uint64_t pid,uint64_t tid){static unsigned n=0;if(n<4){kernel_log("DEBUG: scheduler selected task=");kernel_log_dec(id);kernel_log(" tid=");kernel_log_dec(tid);kernel_log(" pid=");kernel_log_dec(pid);kernel_log("\r\n");n++;}}
static void trace_switched(void){static unsigned n=0;if(n<4){kernel_log("DEBUG: context_switch returned\r\n");n++;}}
static void trace_switching(void){static unsigned n=0;if(n<4){kernel_log("DEBUG: switching context\r\n");n++;}}
static void trace_resumed(void){static unsigned n=0;if(n<4){kernel_log("DEBUG: resumed task id=");kernel_log_dec(tasks[cpu_current[sched_cpu()]].id);kernel_log("\r\n");n++;}}
static __attribute__((noreturn)) void task_bootstrap(void){
    rix_task_t *t=&tasks[cpu_current[sched_cpu()]];
    if(t->process_pid){
        serial_write("BOOT: userspace bootstrap selected\r\n");
        {static unsigned n=0;if(n<2){kernel_log("DEBUG: userspace bootstrap begin pid=");kernel_log_dec(t->process_pid);kernel_log("\r\n");n++;}}
        rix_process_t *p=process_lookup(t->process_pid);
        if(!p){kernel_log("DEBUG: bootstrap process lookup FAILED\r\n");task_returned();}
        if(process_activate_user_entry(t->process_pid)!=0){serial_write("BOOT: userspace CR3 activation FAILED\r\n");kernel_log("DEBUG: bootstrap process_activate FAILED\r\n");task_returned();}
        if(process_validate_user_entry(t->process_pid,t->user_entry,t->user_stack)!=0){
            kernel_log("DEBUG: bootstrap user entry validation FAILED\r\n");
            task_returned();
        }
        serial_write("BOOT: userspace CR3 activation OK\r\n");
        boot_user_entry_marker();
        {static unsigned n=0;if(n<2){kernel_log("DEBUG: entering ring3\r\n");kernel_log("RING3: iretq prepare rip=");kernel_log_hex(t->user_entry);kernel_log(" rsp=");kernel_log_hex(t->user_stack);kernel_log(" cr3=");kernel_log_hex(p->address_space.pml4_phys);kernel_log(" cs=0x1b ss=0x23\r\n");n++;}}
        /* Exactly one enter path runs: context restore for fork children,
         * fresh entry otherwise. Both end in iretq and never return. */
        if(t->user_context_valid)x86_enter_user_context(p->address_space.pml4_phys,&t->user_context);
        else x86_enter_user(p->address_space.pml4_phys,t->user_entry,t->user_stack);
        kernel_log("DEBUG: ring3 entry returned unexpectedly\r\n");
        x86_enter_user_return(p->address_space.pml4_phys,t->user_entry,t->user_stack,t->user_return);
    }
    sti();
    t->entry(t->arg);
    task_returned();
}

int scheduler_init(void){
    ticks=0;next_id=1;
    rix_spin_init(&sched_lock);
    /* Canonical clean FPU image (fninit state). CR4.OSFXSR is already on
     * (vmm_early_init), and CR0.TS/EM are clear, so fxsave/fxrstor are
     * legal from the first switch on. Rows/tasks copy this template. */
    __asm__ volatile("fninit; fxsave %0" : "=m"(fpu_template) :: "memory");
    for(uint32_t c=0;c<SMP_MAX_CPUS;c++){cpu_current[c]=0;cpu_idle_rsp[c]=0;cpu_idle_valid[c]=0;cpu_idle_tmp[c]=0;cpu_preempt_depth[c]=0;cpu_need_resched[c]=0;cpu_quantum_left[c]=RIX_PREEMPT_QUANTUM_TICKS;cpu_in_idle[c]=0;rq_init_locked(&cpu_rq[c]);for(unsigned i=0;i<512;i++)cpu_idle_fpu[c][i]=fpu_template[i];}
    for(uint32_t i=0;i<RIX_MAX_TASKS;i++){
        tasks[i].id=0;tasks[i].state=TASK_UNUSED;tasks[i].rsp=0;tasks[i].entry=NULL;tasks[i].arg=NULL;
        tasks[i].process_pid=0;tasks[i].user_entry=0;tasks[i].user_stack=0;tasks[i].user_return=UINT64_MAX;
        tasks[i].user_context_valid=0;tasks[i].ap_ok=0;tasks[i].tid=0;tasks[i].affinity=~0ULL;tasks[i].run_cpu=~0u;tasks[i].migrations=0;tasks[i].prio=0;tasks[i].run_ticks=0;tasks[i].stamp=0;tasks[i].voluntary=0;tasks[i].involuntary=0;
    }
    tasks[0].id=0;tasks[0].state=TASK_RUNNING;
    /* Phase R1: the BSP boot context owns reserved TID 1 (owner pid 0).
     * thread_alloc for pid 0 cannot fail on a fresh table; a 0 tid here
     * would only mean table exhaustion, which init treats as no-thread. */
    thread_table_init();
    {rix_tid_t boot_tid=0;(void)thread_alloc_locked(0,&boot_tid);tasks[0].tid=boot_tid;}
    return 0;
}
void scheduler_tick(void){ticks++;}
uint64_t scheduler_ticks(void){return ticks;}
/* P3-B slice 2 preempt-disable API (nesting, per-CPU, IRQ-safe).
 * disable/enable only adjust the caller CPU's counter — they never
 * touch the scheduler lock, never sleep, never yield, so they are
 * safe inside allocator/VFS/NVMe critical sections and IRQ handlers.
 * enable() does NOT yield by itself; the pending flag is consumed at
 * the next voluntary yield or IRQ-return boundary, where a task
 * context exists. */
void scheduler_preempt_disable(void){uint32_t me=sched_cpu();if(me<SMP_MAX_CPUS)cpu_preempt_depth[me]++;}
void scheduler_preempt_enable(void){uint32_t me=sched_cpu();if(me<SMP_MAX_CPUS&&cpu_preempt_depth[me])cpu_preempt_depth[me]--;}
int scheduler_preempt_is_disabled(void){uint32_t me=sched_cpu();return me<SMP_MAX_CPUS&&cpu_preempt_depth[me]!=0;}
int scheduler_need_resched(void){uint32_t me=sched_cpu();return me<SMP_MAX_CPUS&&cpu_need_resched[me]!=0;}
void scheduler_clear_need_resched(void){uint32_t me=sched_cpu();if(me<SMP_MAX_CPUS)cpu_need_resched[me]=0;}
/* Phase E7: symmetric quanta. Only the PIT owner drives (APs have no
 * PIT; if IRQ0 ever reached an AP it safely no-ops). The tick pets every
 * ONLINE CPU's quantum row, not just the BSP's: cross-CPU plain volatile
 * stores, benign under x86 TSO (a stale read delays one quantum by one
 * tick at worst). On an AP row expiry with AP-eligible work runnable, the
 * AP is armed and kicked with a targeted WAKEUP IPI: the IPI both breaks
 * hlt and, via the IPI-return yield, preempts a never-yielding spinner.
 * Rate is bounded by the quantum (<=10 Hz per AP) and gated on eligible
 * work and online>1, so UP boots and idle systems send zero IPIs. */
static int ap_eligible_runnable(void){
    for(uint32_t i=1;i<RIX_MAX_TASKS;i++)
        if(tasks[i].state==TASK_RUNNABLE&&tasks[i].process_pid==0&&tasks[i].ap_ok)return 1;
    return 0;
}
static void preempt_arm_cpu(uint32_t c,int is_ap){
    if(cpu_preempt_depth[c])return;
    if(scheduler_runnable_count()<2)return;
    cpu_need_resched[c]=1;
    if(is_ap)(void)smp_wakeup((size_t)c);
}
void scheduler_preempt_tick(void){
    uint32_t me=sched_cpu();
    if(me>=(uint32_t)SMP_MAX_CPUS)return;
    if((int)me!=smp_bsp_index())return;
    if(cpu_quantum_left[me])cpu_quantum_left[me]--;
    if(!cpu_quantum_left[me]){cpu_quantum_left[me]=RIX_PREEMPT_QUANTUM_TICKS;preempt_arm_cpu(me,0);}
    if(smp_online_count()<=1)return;
    if(!ap_eligible_runnable())return;
    for(uint32_t c=0;c<(uint32_t)SMP_MAX_CPUS;c++){
        if((int)c==smp_bsp_index())continue;
        if(smp_cpu_state((size_t)c)!=SMP_CPU_ONLINE)continue;
        if(cpu_quantum_left[c])cpu_quantum_left[c]--;
        if(cpu_quantum_left[c])continue;
        cpu_quantum_left[c]=RIX_PREEMPT_QUANTUM_TICKS;
        preempt_arm_cpu(c,1);
    }
}
/* IRQ/IPI-return helper: call AFTER EOI, before iret. Returns 1 when the
 * caller should perform a voluntary yield now (flag set, preemption
 * enabled, still more than one runnable task). Keeps the EOI-first
 * order so the APIC ISR bit is clear across the switch.
 * Phase E7 gates: skip while this CPU is hlt-parked (its cpu_current is
 * a stale slot — yielding would switch with a foreign task context) and
 * unless the local current slot is genuinely RUNNING (a recycled slot
 * could otherwise corrupt another CPU's live task). A skipped pickup
 * only delays AP work (BSP backstops it), never hangs. BSP behavior is
 * unchanged (never parked, current always RUNNING when it matters). */
int scheduler_should_yield_from_irq(void){
    uint32_t me=sched_cpu();
    if(me>=SMP_MAX_CPUS||cpu_preempt_depth[me]||!cpu_need_resched[me])return 0;
    if(cpu_in_idle[me])return 0;
    uint32_t cur=cpu_current[me];
    if(cur>=RIX_MAX_TASKS||tasks[cur].state!=TASK_RUNNING)return 0;
    if(scheduler_runnable_count()<2){cpu_need_resched[me]=0;cpu_quantum_left[me]=RIX_PREEMPT_QUANTUM_TICKS;return 0;}
    return 1;
}
rix_task_id_t scheduler_current_id(void){return tasks[cpu_current[sched_cpu()]].id;}
uint32_t scheduler_runnable_count(void){uint32_t n=0;for(uint32_t i=0;i<RIX_MAX_TASKS;i++)if(tasks[i].state==TASK_RUNNABLE||tasks[i].state==TASK_RUNNING)n++;return n;}
/* Phase P3 backend: true blocking. prepare() reserves a waiter slot
 * (id = current task) and stashes the binding in the task; block()
 * marks it BLOCKED (selection skips it from then on); take() consumes
 * one wakeup and drops the waiter; abort() unwinds episodes that never
 * consumed a wakeup (EINTR) without consuming; wake_queue() marks every
 * task bound to wq RUNNABLE (spurious-safe: takers re-check). Exit
 * paths drop live bindings so waiter slots never leak. */
int scheduler_wait_prepare(rix_waitqueue_t *wq, rix_wait_handle_t *out){
    if(!wq||!out)return -1;
    uint32_t me=sched_cpu();
    if(me>=SMP_MAX_CPUS)return -1;
    rix_task_t *t=&tasks[cpu_current[me]];
    if(rix_waitqueue_prepare(wq,(uint64_t)t->id,out)!=0)return -1;
    t->wait_wq=wq;t->wait_handle=*out;
    return 0;
}
void scheduler_block_current(void){
    uint32_t me=sched_cpu();
    if(me>=SMP_MAX_CPUS)return;
    uint64_t irq;rix_spin_lock_irqsave(&sched_lock,&irq);
    rix_task_t *t=&tasks[cpu_current[me]];
    /* Phase R4: blocking bypasses the yield-old flip, so close the
     * RUNNING interval here (the later yield sees BLOCKED and skips —
     * no double count; the next claim re-stamps). */
    if(t->state==TASK_RUNNING){t->run_ticks+=ticks-t->stamp;t->state=TASK_BLOCKED;}
    else if(t->state==TASK_RUNNABLE)t->state=TASK_BLOCKED;
    /* Phase R2: BLOCKED is selectable by nobody — drop all bits (no-op
     * when the task was RUNNING, whose bits are already absent). */
    rq_remove_all_locked((uint32_t)(t-tasks));
    rix_spin_unlock_irqrestore(&sched_lock,irq);
}
int scheduler_wait_take(void){
    uint32_t me=sched_cpu();
    if(me>=SMP_MAX_CPUS)return 1;
    rix_task_t *t=&tasks[cpu_current[me]];
    if(!t->wait_wq)return 1;
    int rc=rix_waitqueue_take_wakeup(t->wait_wq,t->wait_handle);
    (void)rix_waitqueue_remove(t->wait_wq,t->wait_handle);
    t->wait_wq=NULL;t->wait_handle=(rix_wait_handle_t){0,0};
    return rc;
}
void scheduler_wait_abort(void){
    uint32_t me=sched_cpu();
    if(me>=SMP_MAX_CPUS)return;
    uint64_t irq;rix_spin_lock_irqsave(&sched_lock,&irq);
    rix_task_t *t=&tasks[cpu_current[me]];
    if(t->wait_wq){(void)rix_waitqueue_remove(t->wait_wq,t->wait_handle);t->wait_wq=NULL;t->wait_handle=(rix_wait_handle_t){0,0};}
    /* Phase R2: only a genuine BLOCKED->RUNNABLE flip re-adds bits (a
     * task woken concurrently is already present — the add would be a
     * harmless no-op, but the state test keeps the invariant exact). */
    if(t->state==TASK_BLOCKED){t->state=TASK_RUNNABLE;rq_add_eligible_locked((uint32_t)(t-tasks));}
    rix_spin_unlock_irqrestore(&sched_lock,irq);
}
void scheduler_wake_queue(rix_waitqueue_t *wq){
    if(!wq)return;
    (void)rix_waitqueue_wake_all(wq);
    int ap_work=0;
    uint64_t irq;rix_spin_lock_irqsave(&sched_lock,&irq);
    for(uint32_t i=0;i<RIX_MAX_TASKS;i++){
        if(tasks[i].state==TASK_BLOCKED&&tasks[i].wait_wq==wq){tasks[i].state=TASK_RUNNABLE;rq_add_eligible_locked(i);if(i!=0&&tasks[i].process_pid==0&&tasks[i].ap_ok)ap_work=1;}
    }
    rix_spin_unlock_irqrestore(&sched_lock,irq);
    /* Phase E7: an AP parked in hlt sleeps through state flips, and the
     * 10 Hz quantum arm is only prompt to 100 ms — kick APs when
     * AP-eligible work just became runnable (broadcast after unlock,
     * never holding the lock across IPI send; no-op when online<=1). */
    if(ap_work)smp_wakeup_aps();
}
void scheduler_wake_pid(uint64_t pid){
    int ap_work=0;
    uint64_t irq;rix_spin_lock_irqsave(&sched_lock,&irq);
    for(uint32_t i=0;i<RIX_MAX_TASKS;i++){
        if(tasks[i].state==TASK_BLOCKED&&tasks[i].process_pid==pid){tasks[i].state=TASK_RUNNABLE;rq_add_eligible_locked(i);if(i!=0&&tasks[i].process_pid==0&&tasks[i].ap_ok)ap_work=1;}
    }
    rix_spin_unlock_irqrestore(&sched_lock,irq);
    /* Phase E7: same cross-CPU kick as wake_queue (signal/exit wakes). */
    if(ap_work)smp_wakeup_aps();
}
/* Phase R1 cross-module wrappers. Both take sched_lock (irqsave — the
 * callers may hold PROCESS_GUARD, the same PROC4->sched edge
 * process_exit_locked already uses for scheduler_wake_pid above).
 * thread_table state itself is only ever touched under sched_lock. */
size_t scheduler_thread_count_for_pid(uint64_t pid){
    uint64_t irq;rix_spin_lock_irqsave(&sched_lock,&irq);
    size_t n=thread_count_locked(pid);
    rix_spin_unlock_irqrestore(&sched_lock,irq);
    return n;
}
void scheduler_thread_detach_pid(uint64_t pid){
    uint64_t irq;rix_spin_lock_irqsave(&sched_lock,&irq);
    thread_detach_pid_locked(pid);
    rix_spin_unlock_irqrestore(&sched_lock,irq);
}
rix_tid_t scheduler_current_tid(void){
    uint32_t me=sched_cpu();
    if(me>=SMP_MAX_CPUS)return 0;
    return tasks[cpu_current[me]].tid;
}
int scheduler_list_threads(rix_thread_info_t *out,size_t capacity,size_t *count){
    if(!count)return -1;
    uint64_t irq;rix_spin_lock_irqsave(&sched_lock,&irq);
    int rc=thread_list_locked(out,capacity,count);
    rix_spin_unlock_irqrestore(&sched_lock,irq);
    return rc;
}
static void task_drop_waiter(rix_task_t *t){
    if(!t||!t->wait_wq)return;
    (void)rix_waitqueue_remove(t->wait_wq,t->wait_handle);
    t->wait_wq=NULL;t->wait_handle=(rix_wait_handle_t){0,0};
}
/* Phase R1: single choke point for task death (sched_lock held at all
 * three call sites: task_returned, scheduler_exit_current, and the
 * resumed-path activate-failure). Marks DEAD, drops the waiter binding,
 * and frees the bound thread exactly once (tid cleared so a recycled
 * slot can never double-free). */
static void task_mark_dead_locked(rix_task_t *t){
    if(!t)return;
    /* Phase R4: close the final RUNNING interval (death bypasses the
     * yield-old flip, so without this the last interval leaks). */
    if(t->state==TASK_RUNNING){t->run_ticks+=ticks-t->stamp;}
    t->state=TASK_DEAD;
    task_drop_waiter(t);
    if(t->tid){thread_free_locked(t->tid);t->tid=0;}
    /* Phase R2: a dead slot is selectable by nobody — drop all bits. */
    rq_remove_all_locked((uint32_t)(t-tasks));
}
void scheduler_dump_states(void){
 kernel_log("DEBUG: TASKS run=");kernel_log_dec(scheduler_runnable_count());kernel_log(" curidx=");
 kernel_log_dec(cpu_current[sched_cpu()]);kernel_log("\r\n");
  for(uint32_t i=0;i<RIX_MAX_TASKS;i++){
   if(tasks[i].state==TASK_UNUSED)continue;
   kernel_log("DEBUG: TASK i=");kernel_log_dec(i);kernel_log(" id=");kernel_log_dec(tasks[i].id);
   kernel_log(" tid=");kernel_log_dec(tasks[i].tid);
   kernel_log(" st=");kernel_log_dec((uint64_t)tasks[i].state);kernel_log(" pid=");kernel_log_dec(tasks[i].process_pid);
   kernel_log(i==cpu_current[sched_cpu()]?" CUR":"");kernel_log("\r\n");
  }
}

static int task_alloc(rix_task_t **out){
    for(uint32_t i=1;i<RIX_MAX_TASKS;i++){if(tasks[i].state==TASK_UNUSED||tasks[i].state==TASK_DEAD){*out=&tasks[i];(*out)->wait_wq=NULL;(*out)->wait_handle=(rix_wait_handle_t){0,0};(*out)->tid=0;(*out)->affinity=~0ULL;(*out)->run_cpu=~0u;(*out)->migrations=0;(*out)->prio=0;(*out)->run_ticks=0;(*out)->stamp=0;(*out)->voluntary=0;(*out)->involuntary=0;return 0;}}
    return -1;
}
/* Phase R1: bind a thread object to a freshly allocated slot (lock
 * held). On failure the slot is released back to UNUSED and the caller
 * reports thread exhaustion (-4): no half-created task ever runs. */
static int task_bind_thread_locked(rix_task_t *t){
    rix_tid_t tid=0;
    if(thread_alloc_locked(t->process_pid,&tid)!=0){t->state=TASK_UNUSED;t->tid=0;return -4;}
    t->tid=tid;
    return 0;
}
static void task_init_stack(rix_task_t *t){
    uintptr_t top=(uintptr_t)t->stack+RIX_STACK_SIZE;top&=~(uintptr_t)0xFULL;uint64_t *sp=(uint64_t*)top;
    /* Padding qword first: after the 6 register pops + ret, task entry must
     * observe SysV rsp%16==8 (as if entered via call). Without it every call
     * inside the first task function runs 8 bytes off alignment. */
    *--sp=0;
    *--sp=(uint64_t)(uintptr_t)task_bootstrap;
    for(unsigned r=0;r<6;r++)*--sp=0;
    t->rsp=(uint64_t)(uintptr_t)sp;
    for(unsigned i=0;i<512;i++)t->fpu[i]=fpu_template[i];
}

int scheduler_create_kernel_thread(rix_kernel_thread_fn entry,void *arg,rix_task_id_t *out_id){
    if(!entry)return -1;
    uint64_t irq;rix_spin_lock_irqsave(&sched_lock,&irq);
    rix_task_t*t;if(task_alloc(&t)!=0){rix_spin_unlock_irqrestore(&sched_lock,irq);return -1;}
    t->id=next_id++;if(!t->id)t->id=next_id++;t->entry=entry;t->arg=arg;t->process_pid=0;
    t->user_entry=0;t->user_stack=0;t->user_return=UINT64_MAX;t->user_context_valid=0;t->state=TASK_RUNNABLE;t->ap_ok=0;task_init_stack(t);
    if(task_bind_thread_locked(t)!=0){rix_spin_unlock_irqrestore(&sched_lock,irq);return -4;}
    /* Phase R2: a fresh RUNNABLE joins every eligible CPU's mask. */
    rq_add_eligible_locked((uint32_t)(t-tasks));
    rix_spin_unlock_irqrestore(&sched_lock,irq);
    /* E2: parked APs predate the scheduler and only hlt-wake on IPI.
     * Broadcast after unlock (never holding the lock across IPI send);
     * no-op when online<=1, so UP boots send zero IPIs. */
    smp_wakeup_aps();
    if(out_id)*out_id=t->id;
    return 0;
}

int scheduler_create_user_process(uint64_t pid,uint64_t entry,uint64_t user_stack,rix_task_id_t *out_id){
    if(!pid||!entry||!user_stack){kernel_log("DEBUG: scheduler task create fail stage=args\r\n");return -1;}
    rix_process_t*p=process_lookup(pid);if(!p||!p->address_space.pml4_phys||!p->kernel_stack){kernel_log("DEBUG: scheduler task create fail stage=lookup pid=");kernel_log_dec(pid);kernel_log("\r\n");return -2;}
    uint64_t irq;rix_spin_lock_irqsave(&sched_lock,&irq);
    rix_task_t*t;if(task_alloc(&t)!=0){kernel_log("DEBUG: scheduler task create fail stage=task-alloc\r\n");rix_spin_unlock_irqrestore(&sched_lock,irq);return -3;}
    t->id=next_id++;if(!t->id)t->id=next_id++;t->entry=NULL;t->arg=NULL;t->process_pid=pid;
    t->user_entry=entry;t->user_stack=user_stack;t->user_return=UINT64_MAX;t->user_context_valid=0;t->state=TASK_RUNNABLE;t->ap_ok=0;task_init_stack(t);
    if(task_bind_thread_locked(t)!=0){kernel_log("DEBUG: scheduler task create fail stage=thread-alloc\r\n");rix_spin_unlock_irqrestore(&sched_lock,irq);return -4;}
    rq_add_eligible_locked((uint32_t)(t-tasks));
    rix_spin_unlock_irqrestore(&sched_lock,irq);
    /* E2: parked APs predate the scheduler and only hlt-wake on IPI.
     * Broadcast after unlock (never holding the lock across IPI send);
     * no-op when online<=1, so UP boots send zero IPIs. */
    smp_wakeup_aps();
    if(out_id)*out_id=t->id;
    return 0;
}

int scheduler_create_fork_child(uint64_t pid,uint64_t entry,uint64_t user_stack,uint64_t return_value,rix_task_id_t*out_id){
    if(!pid||!entry||!user_stack)return -1;
    rix_process_t*p=process_lookup(pid);if(!p||!p->address_space.pml4_phys||!p->kernel_stack)return -1;
    uint64_t irq;rix_spin_lock_irqsave(&sched_lock,&irq);
    rix_task_t*t;if(task_alloc(&t)!=0){rix_spin_unlock_irqrestore(&sched_lock,irq);return -1;}
    t->id=next_id++;if(!t->id)t->id=next_id++;t->entry=NULL;t->arg=NULL;t->process_pid=pid;
    t->user_entry=entry;t->user_stack=user_stack;t->user_return=return_value;t->user_context_valid=0;t->state=TASK_RUNNABLE;t->ap_ok=0;task_init_stack(t);
    if(task_bind_thread_locked(t)!=0){rix_spin_unlock_irqrestore(&sched_lock,irq);return -4;}
    rq_add_eligible_locked((uint32_t)(t-tasks));
    rix_spin_unlock_irqrestore(&sched_lock,irq);
    /* E2: parked APs predate the scheduler and only hlt-wake on IPI.
     * Broadcast after unlock (never holding the lock across IPI send);
     * no-op when online<=1, so UP boots send zero IPIs. */
    smp_wakeup_aps();
    if(out_id)*out_id=t->id;
    return 0;
}

int scheduler_create_fork_child_context(uint64_t pid,const rix_user_context_t*context,rix_task_id_t*out_id){
    if(!pid||!context)return -1;
    rix_process_t*p=process_lookup(pid);if(!p||!p->address_space.pml4_phys||!p->kernel_stack)return -1;
    uint64_t irq;rix_spin_lock_irqsave(&sched_lock,&irq);
    rix_task_t*t;if(task_alloc(&t)!=0){rix_spin_unlock_irqrestore(&sched_lock,irq);return -1;}
    t->id=next_id++;if(!t->id)t->id=next_id++;t->entry=NULL;t->arg=NULL;t->process_pid=pid;
    t->user_entry=context->rip;t->user_stack=context->rsp;t->user_return=0;t->user_context=*context;t->user_context.rax=0;
    t->user_context_valid=1;t->state=TASK_RUNNABLE;t->ap_ok=0;task_init_stack(t);
    if(task_bind_thread_locked(t)!=0){rix_spin_unlock_irqrestore(&sched_lock,irq);return -4;}
    rq_add_eligible_locked((uint32_t)(t-tasks));
    rix_spin_unlock_irqrestore(&sched_lock,irq);
    /* E2: parked APs predate the scheduler and only hlt-wake on IPI.
     * Broadcast after unlock (never holding the lock across IPI send);
     * no-op when online<=1, so UP boots send zero IPIs. */
    smp_wakeup_aps();
    if(out_id)*out_id=t->id;
    return 0;
}

__attribute__((noreturn)) void scheduler_exit_current(void){
    cli();
    rix_spin_lock(&sched_lock);
    task_mark_dead_locked(&tasks[cpu_current[sched_cpu()]]);
    rix_spin_unlock(&sched_lock);
    for(;;) scheduler_yield();
}

/* DEBUG-only isolation: skip the context-switch call itself (selection
 * and process activation still run). Default 0. If a bootloop vanishes
 * with this set, the switch/task-stack path is implicated. */
#define RIX_DEBUG_NO_CTX_SWITCH 0
/* Cooperative yield (P3-B slice 2 shape). Consumes the PIT-armed
 * need-resched flag: a voluntary yield always re-arms the quantum
 * (it IS a schedule point), and the same reload happens on resume
 * after a switch. No IRQ-context switch exists yet; the IRQ-return
 * path (EOI first, then scheduler_should_yield_from_irq + yield) is
 * the only other consumer. See P1-revert in SMP_DESIGN.md for why
 * async IRQ-context switching stays out. */
void scheduler_yield(void){
    static unsigned boot_marker;
    if (boot_marker++ < 2) serial_write("BOOT: scheduler yield\r\n");
    uint64_t flags=read_rflags();
    trace_flags();
    cli();
    uint32_t self=sched_cpu();
    /* Phase R4: capture the armed flag BEFORE the E7 block below clears
     * it — a yield that consumes an armed quantum is an involuntary
     * (preempted) yield, otherwise voluntary. The flag belongs to this
     * CPU, and yield always acts on this CPU's current task. */
    int yield_armed=0;
    /* Phase E7: bounded AP-quantum consumption proof. Fires when an AP
     * enters a yield with its tick-armed flag set (voluntary or
     * IPI-return): the mechanism is live on that CPU. It does not by
     * itself prove time-sharing — the spinner-trio placement lines do. */
    if(self<SMP_MAX_CPUS){yield_armed=cpu_need_resched[self];int bsp=(int)self==smp_bsp_index();int ap_armed=!bsp&&cpu_need_resched[self];cpu_need_resched[self]=0;cpu_quantum_left[self]=RIX_PREEMPT_QUANTUM_TICKS;{static unsigned n=0;if(ap_armed&&n<3){kernel_log("APREEMPT cpu=");kernel_log_dec(self);kernel_log("\r\n");n++;}}}
    trace_yield_begin();
    /* E1: selection + publish under the scheduler lock (IRQs already off,
     * so plain lock/unlock; IRQ posture across the switch is unchanged).
     * The lock is released before rix_context_switch and never held
     * across process_activate. */
    rix_spin_lock(&sched_lock);
    uint32_t me=sched_cpu();
    uint32_t old=cpu_current[me],next=old;
    trace_searching();
    /* Phase R4 flip-first: the yielding task rejoins BEFORE the pick so
     * preference/streak see the full contending set, and the RUNNING
     * interval closes exactly once per deschedule. (Pick-before-flip
     * structurally bars self-pick — the yielder is bit-absent at pick
     * time, so every yield is forced to switch and streak can never
     * exceed 1. Measured live 2026-09-15 as a 7,8,7,8 lockstep on UP
     * that neutralized priority while host tests passed in isolation.
     * Flip-first restores the forced share; rotation dynamics for equal
     * priority are unchanged — the cursor still bars head-of-line.) */
    int old_flipped=0;
    if(tasks[old].state==TASK_RUNNING){tasks[old].state=TASK_RUNNABLE;tasks[old].run_ticks+=ticks-tasks[old].stamp;old_flipped=1;rq_add_eligible_locked(old);}
    /* Phase R2: verify the mask cache, then pick from this CPU's own
     * runqueue (fail-stops on drift — see rq_verify_locked). */
    rq_verify_locked();
    next=sched_pick_locked(me,old);
    trace_old_next(old,next);
    if(next==old){
        if(old_flipped){
            /* Sole runnable (or lone pick): reclaim — back to RUNNING
             * with bits dropped and the interval re-stamped, then
             * continue on this stack. Same critical section, so no AP
             * can steal mid-reclaim. Yield classification counts real
             * switches only (switch branch below). */
            tasks[old].state=TASK_RUNNING;rq_remove_all_locked(old);tasks[old].stamp=ticks;
        }
        /* Nothing else runnable. A RUNNING current simply continues
         * (legacy). A DEAD current on an AP returns to its idle hlt
         * loop (scratch save slot: the dead slot may already be
         * recycled); the BSP keeps the legacy spin. */
        if(tasks[old].state==TASK_RUNNING){rix_spin_unlock(&sched_lock);if(flags&0x200ULL)sti();return;}
        if(me<SMP_MAX_CPUS&&cpu_idle_valid[me]){
            uint64_t idle=cpu_idle_rsp[me];
            rix_spin_unlock(&sched_lock);
            fpu_switch(tasks[old].fpu,cpu_idle_fpu[me]);
            rix_context_switch(&cpu_idle_tmp[me],idle);
            if(flags&0x200ULL)sti();
            return;
        }
        rix_spin_unlock(&sched_lock);if(flags&0x200ULL)sti();return;
    }
    /* Genuine switch (next != old): classify the yield — an armed
     * quantum consumed means preempted, else voluntary. Only a flipped
     * old descheduled here (BLOCKED/DEAD olds blocked/died elsewhere
     * and are not yields). */
    if(old_flipped){if(yield_armed)tasks[old].involuntary++;else tasks[old].voluntary++;}
    trace_old_updated(old);
    trace_activating(tasks[next].process_pid);
    /* Keep the current CR3 until the stack switch completes.  Loading the
       next process root here used to execute the remainder of scheduler_yield
       on the old task's stack under the new address space.  That invariant
       is fragile on physical CPUs and can fail immediately after MOV CR3.
       The resumed-task path below activates the selected process after the
       switch; task_bootstrap defers the first user load to x86_enter_user(). */
    tasks[next].state=TASK_RUNNING;cpu_current[sched_cpu()]=next;
    /* Phase R2: claim — RUNNING is selectable by nobody. */
    rq_remove_all_locked(next);
    /* Phase R3: migration accounting. Phase R4: stamp the claim (the
     * RUNNING interval starts now; closed at the next yield-old, block
     * or death — exactly one close per claim by construction). */
    rq_note_claim_locked(me,next);
    tasks[next].stamp=ticks;
    cr3trace_push(3,(uint64_t)tasks[old].id,(uint64_t)tasks[next].id,0);
    trace_selected(tasks[next].id,tasks[next].process_pid,tasks[next].tid);
    trace_first_task(next);
    trace_switching();
#if RIX_DEBUG_NO_CTX_SWITCH
    {static unsigned n=0;if(n<4){kernel_log("DEBUG: context switch SKIPPED\r\n");n++;}}
    if(tasks[old].state==TASK_RUNNABLE)tasks[old].state=TASK_RUNNING;
    /* Phase R2: the debug path restores old to RUNNING — drop the bits
     * the yield-old flip above just added (next stays claimed: it
     * remains TASK_RUNNING with no bits, consistent). */
    rq_remove_all_locked(old);
    if(tasks[old].process_pid){(void)process_activate(tasks[old].process_pid);}
    else if(tasks[old].id==0){(void)process_activate(0);}
    cpu_current[sched_cpu()]=old;
    rix_spin_unlock(&sched_lock);
    if(flags&0x200ULL){sti();}
    return;
#endif
    rix_spin_unlock(&sched_lock);
    fpu_switch(tasks[old].fpu,tasks[next].fpu);
    rix_context_switch(&tasks[old].rsp,tasks[next].rsp);
    trace_switched();
    trace_resumed();
    /* The IRQ-return yield runs on the INTERRUPTED task's kernel stack:
     * rix_context_switch saved the preemption RSP into tasks[old].rsp,
     * so no additional IRQ-frame accounting is needed. The resumed-task
     * path below re-activates the resumed address space exactly like a
     * voluntary yield. */
    /* The context switch returns in the task that was waiting in this
       function. The address space must follow the resumed task, not the task
       that ran immediately before it. */
    rix_task_t*resumed=&tasks[cpu_current[sched_cpu()]];
    if(resumed->process_pid){if(process_activate(resumed->process_pid)!=0){rix_spin_lock(&sched_lock);task_mark_dead_locked(resumed);rix_spin_unlock(&sched_lock);}}
    else if(resumed->id==0){(void)process_activate(0);}
    if(flags&0x200ULL)sti();
}

/* Phase E2 AP idle entry. Captures this AP's stack as the idle context
 * once, then hlt-parks, running ap_ok kernel-thread tasks as the BSP
 * publishes them (wakeup IPIs arrive with IF=1). No stack variables
 * live across the switches below: only globals are touched. */
__attribute__((noreturn)) void scheduler_ap_idle(void){
    int id=smp_cpu_id();
    if(id<0||id>=SMP_MAX_CPUS){for(;;)__asm__ volatile("sti; hlt" ::: "memory");}
    const smp_cpu_t*cpu=smp_cpu((size_t)id);
    if(!cpu||cpu->is_bsp){for(;;)__asm__ volatile("sti; hlt" ::: "memory");}
    uint32_t me=(uint32_t)id;
    for(;;){
        /* Re-publish every iteration (not just once): APs park before
         * scheduler_init runs, and init zeroes this table — the slot
         * self-heals on the next loop. RSP is stable (this same stack). */
        uint64_t rsp;__asm__ volatile("mov %%rsp,%0":"=r"(rsp)::"memory");
        cpu_idle_rsp[me]=rsp;cpu_idle_valid[me]=1;
        __asm__ volatile("" ::: "memory");
        __asm__ volatile("sti" ::: "memory");
        /* E2 cooperative pick. sched_lock via irqsave (not plain): a PIT
         * tick may preempt this window once timer preemption lands, and
         * a plain spin here would deadlock against it. */
        uint64_t ap_irq;
        rix_spin_lock_irqsave(&sched_lock, &ap_irq);
        uint32_t old=cpu_current[me];
        /* Phase R2: same verify+pick as the yield path (behavior
         * preserved exactly: next==old still hlt-parks, relying on the
         * next wake kick like before). */
        rq_verify_locked();
        uint32_t next=sched_pick_locked(me,old);
        if(next==old||tasks[next].state!=TASK_RUNNABLE){
            /* Phase E7: mark the hlt window BEFORE unlock (IF is masked
             * here, so no IPI can slip between the mark and the hlt
             * with the gate seeing a stale clear). A kick landing after
             * the mark is seen as parked and skipped — it only delays
             * AP work to the next quantum arm (the BSP backstops it). */
            cpu_in_idle[me]=1;
            rix_spin_unlock_irqrestore(&sched_lock, ap_irq);
            __asm__ volatile("hlt" ::: "memory");
            cpu_in_idle[me]=0;
            continue;
        }
        cpu_in_idle[me]=0;
        tasks[next].state=TASK_RUNNING;cpu_current[me]=next;
        /* Phase R2: claim — RUNNING is selectable by nobody. */
        rq_remove_all_locked(next);
        /* Phase R3: migration accounting. Phase R4: stamp the claim. */
        rq_note_claim_locked(me,next);
        tasks[next].stamp=ticks;
        cr3trace_push(3,(uint64_t)tasks[old].id,(uint64_t)tasks[next].id,0);
        rix_spin_unlock_irqrestore(&sched_lock, ap_irq);
        fpu_switch(cpu_idle_fpu[me],tasks[next].fpu);
        rix_context_switch(&cpu_idle_rsp[me],tasks[next].rsp);
    }
}
