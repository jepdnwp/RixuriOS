#include "scheduler.h"
#include "../arch/x86_64/irq.h"
#include "../process/process.h"
#include "../arch/x86_64/user_entry.h"
#include <stddef.h>
#include <stdint.h>

#define RIX_MAX_TASKS 32
#define RIX_STACK_SIZE 16384

typedef enum { TASK_UNUSED=0, TASK_RUNNABLE=1, TASK_RUNNING=2, TASK_DEAD=3 } task_state_t;
typedef struct { rix_task_id_t id; task_state_t state; uint64_t rsp; rix_kernel_thread_fn entry; void *arg; uint64_t process_pid; uint64_t user_entry; uint64_t user_stack; uint8_t stack[RIX_STACK_SIZE] __attribute__((aligned(16))); } rix_task_t;

extern void rix_context_switch(uint64_t *old_rsp,uint64_t new_rsp);
static volatile uint64_t ticks;
static rix_task_t tasks[RIX_MAX_TASKS];
static uint32_t current_index;
static rix_task_id_t next_id;

static uint64_t read_rflags(void){uint64_t v;__asm__ volatile("pushfq; popq %0":"=r"(v)::"memory");return v;}
static void cli(void){__asm__ volatile("cli" ::: "memory");}
static void sti(void){__asm__ volatile("sti" ::: "memory");}

static void task_returned(void){ tasks[current_index].state=TASK_DEAD; for(;;) scheduler_yield(); }
static void task_bootstrap(void){
    rix_task_t *t=&tasks[current_index];
    if(t->process_pid){
        rix_process_t *p=process_lookup(t->process_pid);
        if(!p||process_activate(t->process_pid)!=0)task_returned();
        x86_enter_user(p->address_space.pml4_phys,t->user_entry,t->user_stack);
    }
    sti();
    t->entry(t->arg);
    task_returned();
}

int scheduler_init(void){
    ticks=0;current_index=0;next_id=1;
    for(uint32_t i=0;i<RIX_MAX_TASKS;i++){tasks[i].id=0;tasks[i].state=TASK_UNUSED;tasks[i].rsp=0;tasks[i].entry=NULL;tasks[i].arg=NULL;tasks[i].process_pid=0;tasks[i].user_entry=0;tasks[i].user_stack=0;}
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
    *--sp=(uint64_t)(uintptr_t)task_bootstrap;
    for(unsigned r=0;r<6;r++)*--sp=0;
    t->rsp=(uint64_t)(uintptr_t)sp;
}

int scheduler_create_kernel_thread(rix_kernel_thread_fn entry,void *arg,rix_task_id_t *out_id){
    if(!entry)return -1;
    rix_task_t *t;if(task_alloc(&t)!=0)return -1;
    t->id=next_id++;if(!t->id)t->id=next_id++;t->entry=entry;t->arg=arg;t->process_pid=0;t->user_entry=0;t->user_stack=0;t->state=TASK_RUNNABLE;task_init_stack(t);
    if(out_id)*out_id=t->id;
    return 0;
}

int scheduler_create_user_process(uint64_t pid,uint64_t entry,uint64_t user_stack,rix_task_id_t *out_id){
    if(!pid||!entry||!user_stack)return -1;
    rix_process_t *p=process_lookup(pid);if(!p||!p->address_space.pml4_phys||!p->kernel_stack)return -1;
    rix_task_t *t;if(task_alloc(&t)!=0)return -1;
    t->id=next_id++;if(!t->id)t->id=next_id++;t->entry=NULL;t->arg=NULL;t->process_pid=pid;t->user_entry=entry;t->user_stack=user_stack;t->state=TASK_RUNNABLE;task_init_stack(t);
    if(out_id)*out_id=t->id;
    return 0;
}

__attribute__((noreturn)) void scheduler_exit_current(void){
    cli();
    tasks[current_index].state=TASK_DEAD;
    for(;;) scheduler_yield();
}

void scheduler_yield(void){
    uint64_t flags=read_rflags();cli();
    uint32_t old=current_index,next=old;
    for(uint32_t n=1;n<RIX_MAX_TASKS;n++){uint32_t i=(old+n)%RIX_MAX_TASKS;if(tasks[i].state==TASK_RUNNABLE){next=i;break;}}
    if(next==old){if(flags&0x200ULL)sti();return;}
    if(tasks[old].state==TASK_RUNNING)tasks[old].state=TASK_RUNNABLE;
    if(tasks[next].process_pid){if(process_activate(tasks[next].process_pid)!=0){tasks[next].state=TASK_DEAD;if(flags&0x200ULL)sti();return;}}
    else if(tasks[next].id==0){if(process_activate(0)!=0){if(flags&0x200ULL)sti();return;}}
    tasks[next].state=TASK_RUNNING;current_index=next;
    rix_context_switch(&tasks[old].rsp,tasks[next].rsp);
    /* The context switch returns in the task that was waiting in this
       function. The address space must follow the resumed task, not the task
       that ran immediately before it. */
    rix_task_t *resumed=&tasks[current_index];
    if(resumed->process_pid){
        if(process_activate(resumed->process_pid)!=0)resumed->state=TASK_DEAD;
    }else if(resumed->id==0){
        (void)process_activate(0);
    }
    if(flags&0x200ULL)sti();
}
