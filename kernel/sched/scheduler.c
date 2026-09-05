#include "scheduler.h"
#include "../arch/x86_64/irq.h"
#include <stddef.h>
#include <stdint.h>

#define RIX_MAX_TASKS 32
#define RIX_STACK_SIZE 16384

typedef enum { TASK_UNUSED=0, TASK_RUNNABLE=1, TASK_RUNNING=2, TASK_DEAD=3 } task_state_t;
typedef struct { rix_task_id_t id; task_state_t state; uint64_t rsp; rix_kernel_thread_fn entry; void *arg; uint8_t stack[RIX_STACK_SIZE] __attribute__((aligned(16))); } rix_task_t;

extern void rix_context_switch(uint64_t *old_rsp,uint64_t new_rsp);
static volatile uint64_t ticks;
static rix_task_t tasks[RIX_MAX_TASKS];
static uint32_t current_index;
static rix_task_id_t next_id;

static void task_returned(void){ tasks[current_index].state=TASK_DEAD; for(;;) scheduler_yield(); }
static void task_bootstrap(void){ rix_task_t *t=&tasks[current_index]; t->entry(t->arg); task_returned(); }
static void sched_irq(unsigned irq,const void *frame){ (void)irq; (void)frame; scheduler_tick(); }

int scheduler_init(void){
    ticks=0;current_index=0;next_id=1;
    for(uint32_t i=0;i<RIX_MAX_TASKS;i++){tasks[i].id=0;tasks[i].state=TASK_UNUSED;tasks[i].rsp=0;tasks[i].entry=NULL;tasks[i].arg=NULL;}
    tasks[0].id=0;tasks[0].state=TASK_RUNNING;
    return irq_register(0,sched_irq);
}
void scheduler_tick(void){ticks++;}
uint64_t scheduler_ticks(void){return ticks;}
rix_task_id_t scheduler_current_id(void){return tasks[current_index].id;}
uint32_t scheduler_runnable_count(void){uint32_t n=0;for(uint32_t i=0;i<RIX_MAX_TASKS;i++)if(tasks[i].state==TASK_RUNNABLE||tasks[i].state==TASK_RUNNING)n++;return n;}

int scheduler_create_kernel_thread(rix_kernel_thread_fn entry,void *arg,rix_task_id_t *out_id){
    if(!entry)return -1;
    for(uint32_t i=1;i<RIX_MAX_TASKS;i++){
        if(tasks[i].state!=TASK_UNUSED&&tasks[i].state!=TASK_DEAD)continue;
        rix_task_t *t=&tasks[i];t->id=next_id++;if(!t->id)t->id=next_id++;t->entry=entry;t->arg=arg;t->state=TASK_RUNNABLE;
        uintptr_t top=(uintptr_t)t->stack+RIX_STACK_SIZE;top&=~(uintptr_t)0xFULL;uint64_t *sp=(uint64_t*)top;
        *--sp=(uint64_t)(uintptr_t)task_bootstrap;
        for(unsigned r=0;r<6;r++)*--sp=0;
        t->rsp=(uint64_t)(uintptr_t)sp;if(out_id)*out_id=t->id;return 0;
    }
    return -1;
}

void scheduler_yield(void){
    uint32_t old=current_index,next=old;
    for(uint32_t n=1;n<RIX_MAX_TASKS;n++){uint32_t i=(old+n)%RIX_MAX_TASKS;if(tasks[i].state==TASK_RUNNABLE){next=i;break;}}
    if(next==old)return;
    if(tasks[old].state==TASK_RUNNING)tasks[old].state=TASK_RUNNABLE;
    tasks[next].state=TASK_RUNNING;current_index=next;rix_context_switch(&tasks[old].rsp,tasks[next].rsp);
}
