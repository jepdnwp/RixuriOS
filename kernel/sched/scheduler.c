#include "scheduler.h"
#include "../arch/x86_64/irq.h"
#include "../process/process.h"
#include "../arch/x86_64/user_entry.h"
#include "kernel.h"
#include <stddef.h>
#include <stdint.h>

#define RIX_MAX_TASKS 32
#define RIX_STACK_SIZE 16384

typedef enum { TASK_UNUSED=0, TASK_RUNNABLE=1, TASK_RUNNING=2, TASK_DEAD=3 } task_state_t;
typedef struct {
    rix_task_id_t id;
    task_state_t state;
    uint64_t rsp;
    rix_kernel_thread_fn entry;
    void *arg;
    uint64_t process_pid;
    uint64_t user_entry;
    uint64_t user_stack;
    uint64_t user_return;
    uint8_t user_context_valid;
    rix_user_context_t user_context;
    uint8_t stack[RIX_STACK_SIZE] __attribute__((aligned(16)));
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
static uint32_t current_index;
static rix_task_id_t next_id;

static uint64_t read_rflags(void){uint64_t v;__asm__ volatile("pushfq; popq %0":"=r"(v)::"memory");return v;}
static void cli(void){__asm__ volatile("cli" ::: "memory");}
static void sti(void){__asm__ volatile("sti" ::: "memory");}

static __attribute__((noreturn)) void task_returned(void){ tasks[current_index].state=TASK_DEAD; for(;;) scheduler_yield(); }
static void boot_user_entry_marker(uint64_t pid,uint64_t entry,uint64_t stack,uint64_t pml4){kernel_log("BOOT: user entry pid=");kernel_log_dec(pid);kernel_log(" entry=");kernel_log_hex(entry);kernel_log(" stack=");kernel_log_hex(stack);kernel_log(" pml4=");kernel_log_hex(pml4);kernel_log("\r\n");}
static void trace_yield_begin(void){static unsigned n=0;if(n<2){kernel_log("DEBUG: scheduler_yield begin\r\n");n++;}}
static void trace_flags(void){static unsigned n=0;if(n<2){kernel_log("DEBUG: flags read\r\nDEBUG: cli done\r\n");n++;}}
static void trace_searching(void){static unsigned n=0;if(n<2){kernel_log("DEBUG: searching runnable task\r\n");n++;}}
static void trace_old_next(uint32_t old,uint32_t next){static unsigned n=0;if(n<4){kernel_log("DEBUG: old=");kernel_log_dec(tasks[old].id);kernel_log(" next=");kernel_log_dec(tasks[next].id);kernel_log("\r\n");n++;}}
static void trace_old_updated(uint32_t old){static unsigned n=0;if(n<4){kernel_log("DEBUG: old state updated\r\n");(void)old;n++;}}
static void trace_activating(uint64_t pid){static unsigned n=0;if(n<4){kernel_log("DEBUG: activating next process\r\n");(void)pid;n++;}}
/* First user task selected: full transition inputs in one bounded block. */
static void trace_first_task(uint32_t idx){static unsigned n=0;if(n<1&&tasks[idx].process_pid){kernel_log("DEBUG: first task id=");kernel_log_dec(tasks[idx].id);kernel_log(" state=");kernel_log_dec(tasks[idx].state);kernel_log(" pid=");kernel_log_dec(tasks[idx].process_pid);kernel_log(" rsp=");kernel_log_hex(tasks[idx].rsp);kernel_log(" entry=");kernel_log_hex(tasks[idx].user_entry);kernel_log(" user_stack=");kernel_log_hex(tasks[idx].user_stack);kernel_log("\r\n");n++;}}
static void trace_selected(uint64_t id,uint64_t pid){static unsigned n=0;if(n<4){kernel_log("DEBUG: scheduler selected task=");kernel_log_dec(id);kernel_log(" pid=");kernel_log_dec(pid);kernel_log("\r\n");n++;}}
static void trace_switched(void){static unsigned n=0;if(n<4){kernel_log("DEBUG: context_switch returned\r\n");n++;}}
static void trace_switching(void){static unsigned n=0;if(n<4){kernel_log("DEBUG: switching context\r\n");n++;}}
static void trace_resumed(void){static unsigned n=0;if(n<4){kernel_log("DEBUG: resumed task id=");kernel_log_dec(tasks[current_index].id);kernel_log("\r\n");n++;}}
static __attribute__((noreturn)) void task_bootstrap(void){
    rix_task_t *t=&tasks[current_index];
    if(t->process_pid){
        {static unsigned n=0;if(n<2){kernel_log("DEBUG: userspace bootstrap begin pid=");kernel_log_dec(t->process_pid);kernel_log("\r\n");n++;}}
        rix_process_t *p=process_lookup(t->process_pid);
        if(!p){kernel_log("DEBUG: bootstrap process lookup FAILED\r\n");task_returned();}
        if(process_activate(t->process_pid)!=0){kernel_log("DEBUG: bootstrap process_activate FAILED\r\n");task_returned();}
        boot_user_entry_marker(t->process_pid,t->user_entry,t->user_stack,p->address_space.pml4_phys);
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
    ticks=0;current_index=0;next_id=1;
    for(uint32_t i=0;i<RIX_MAX_TASKS;i++){
        tasks[i].id=0;tasks[i].state=TASK_UNUSED;tasks[i].rsp=0;tasks[i].entry=NULL;tasks[i].arg=NULL;
        tasks[i].process_pid=0;tasks[i].user_entry=0;tasks[i].user_stack=0;tasks[i].user_return=UINT64_MAX;
        tasks[i].user_context_valid=0;
    }
    tasks[0].id=0;tasks[0].state=TASK_RUNNING;
    return 0;
}
void scheduler_tick(void){ticks++;}
uint64_t scheduler_ticks(void){return ticks;}
rix_task_id_t scheduler_current_id(void){return tasks[current_index].id;}
uint32_t scheduler_runnable_count(void){uint32_t n=0;for(uint32_t i=0;i<RIX_MAX_TASKS;i++)if(tasks[i].state==TASK_RUNNABLE||tasks[i].state==TASK_RUNNING)n++;return n;}

static int task_alloc(rix_task_t **out){
    for(uint32_t i=1;i<RIX_MAX_TASKS;i++){if(tasks[i].state==TASK_UNUSED||tasks[i].state==TASK_DEAD){*out=&tasks[i];return 0;}}
    return -1;
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
}

int scheduler_create_kernel_thread(rix_kernel_thread_fn entry,void *arg,rix_task_id_t *out_id){
    if(!entry)return -1;
    rix_task_t*t;if(task_alloc(&t)!=0)return -1;
    t->id=next_id++;if(!t->id)t->id=next_id++;t->entry=entry;t->arg=arg;t->process_pid=0;
    t->user_entry=0;t->user_stack=0;t->user_return=UINT64_MAX;t->user_context_valid=0;t->state=TASK_RUNNABLE;task_init_stack(t);
    if(out_id)*out_id=t->id;
    return 0;
}

int scheduler_create_user_process(uint64_t pid,uint64_t entry,uint64_t user_stack,rix_task_id_t *out_id){
    if(!pid||!entry||!user_stack){kernel_log("DEBUG: scheduler task create fail stage=args\r\n");return -1;}
    rix_process_t*p=process_lookup(pid);if(!p||!p->address_space.pml4_phys||!p->kernel_stack){kernel_log("DEBUG: scheduler task create fail stage=lookup pid=");kernel_log_dec(pid);kernel_log("\r\n");return -2;}
    rix_task_t*t;if(task_alloc(&t)!=0){kernel_log("DEBUG: scheduler task create fail stage=task-alloc\r\n");return -3;}
    t->id=next_id++;if(!t->id)t->id=next_id++;t->entry=NULL;t->arg=NULL;t->process_pid=pid;
    t->user_entry=entry;t->user_stack=user_stack;t->user_return=UINT64_MAX;t->user_context_valid=0;t->state=TASK_RUNNABLE;task_init_stack(t);
    if(out_id)*out_id=t->id;
    return 0;
}

int scheduler_create_fork_child(uint64_t pid,uint64_t entry,uint64_t user_stack,uint64_t return_value,rix_task_id_t*out_id){
    if(!pid||!entry||!user_stack)return -1;
    rix_process_t*p=process_lookup(pid);if(!p||!p->address_space.pml4_phys||!p->kernel_stack)return -1;
    rix_task_t*t;if(task_alloc(&t)!=0)return -1;
    t->id=next_id++;if(!t->id)t->id=next_id++;t->entry=NULL;t->arg=NULL;t->process_pid=pid;
    t->user_entry=entry;t->user_stack=user_stack;t->user_return=return_value;t->user_context_valid=0;t->state=TASK_RUNNABLE;task_init_stack(t);
    if(out_id)*out_id=t->id;
    return 0;
}

int scheduler_create_fork_child_context(uint64_t pid,const rix_user_context_t*context,rix_task_id_t*out_id){
    if(!pid||!context)return -1;
    rix_process_t*p=process_lookup(pid);if(!p||!p->address_space.pml4_phys||!p->kernel_stack)return -1;
    rix_task_t*t;if(task_alloc(&t)!=0)return -1;
    t->id=next_id++;if(!t->id)t->id=next_id++;t->entry=NULL;t->arg=NULL;t->process_pid=pid;
    t->user_entry=context->rip;t->user_stack=context->rsp;t->user_return=0;t->user_context=*context;t->user_context.rax=0;
    t->user_context_valid=1;t->state=TASK_RUNNABLE;task_init_stack(t);
    if(out_id)*out_id=t->id;
    return 0;
}

__attribute__((noreturn)) void scheduler_exit_current(void){
    cli();
    tasks[current_index].state=TASK_DEAD;
    for(;;) scheduler_yield();
}

/* DEBUG-only isolation: skip the context-switch call itself (selection
 * and process activation still run). Default 0. If a bootloop vanishes
 * with this set, the switch/task-stack path is implicated. */
#define RIX_DEBUG_NO_CTX_SWITCH 0
void scheduler_yield(void){
    uint64_t flags=read_rflags();
    trace_flags();
    cli();
    trace_yield_begin();
    uint32_t old=current_index,next=old;
    trace_searching();
    for(uint32_t n=1;n<RIX_MAX_TASKS;n++){uint32_t i=(old+n)%RIX_MAX_TASKS;if(tasks[i].state==TASK_RUNNABLE){next=i;break;}}
    trace_old_next(old,next);
    cr3trace_push(3,(uint64_t)tasks[old].id,(uint64_t)tasks[next].id,0);
    if(next==old){if(flags&0x200ULL)sti();return;}
    if(tasks[old].state==TASK_RUNNING)tasks[old].state=TASK_RUNNABLE;
    trace_old_updated(old);
    trace_activating(tasks[next].process_pid);
    if(tasks[next].process_pid){if(process_activate(tasks[next].process_pid)!=0){kernel_log("DEBUG: scheduler process_activate FAILED\r\n");tasks[next].state=TASK_DEAD;if(flags&0x200ULL)sti();return;}}
    else if(tasks[next].id==0){if(process_activate(0)!=0){if(flags&0x200ULL)sti();return;}}
    tasks[next].state=TASK_RUNNING;current_index=next;
    trace_selected(tasks[next].id,tasks[next].process_pid);
    trace_first_task(next);
    trace_switching();
#if RIX_DEBUG_NO_CTX_SWITCH
    {static unsigned n=0;if(n<4){kernel_log("DEBUG: context switch SKIPPED\r\n");n++;}}
    if(tasks[old].state==TASK_RUNNABLE)tasks[old].state=TASK_RUNNING;
    if(tasks[old].process_pid){(void)process_activate(tasks[old].process_pid);}
    else if(tasks[old].id==0){(void)process_activate(0);}
    current_index=old;
    if(flags&0x200ULL){sti();}
    return;
#endif
    rix_context_switch(&tasks[old].rsp,tasks[next].rsp);
    trace_switched();
    trace_resumed();
    /* The context switch returns in the task that was waiting in this
       function. The address space must follow the resumed task, not the task
       that ran immediately before it. */
    rix_task_t*resumed=&tasks[current_index];
    if(resumed->process_pid){if(process_activate(resumed->process_pid)!=0)resumed->state=TASK_DEAD;}
    else if(resumed->id==0){(void)process_activate(0);}
    if(flags&0x200ULL)sti();
}
