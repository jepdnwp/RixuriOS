#include "pit.h"
#include "irq.h"
#include "../../sched/scheduler.h"
#include <stdint.h>
#define PIT_HZ 1193182u
static volatile uint64_t ticks;
static volatile uint64_t preempt_lines;
static inline void outb(uint16_t p,uint8_t v){__asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p));}
int pit_init(uint32_t hz){if(!hz||hz>PIT_HZ)return -1;uint32_t div=PIT_HZ/hz;if(div<1)div=1;if(div>65535)div=65535;outb(0x43,0x36);outb(0x40,(uint8_t)div);outb(0x40,(uint8_t)(div>>8));ticks=0;return irq_register(0,pit_irq);}
/* PIT IRQ (IF=0 entry). Ticks first (timekeeping never depends on
 * scheduling), then arms the scheduler quantum. No context switch
 * here by design (P1 lesson): the IRQ-return path in x86_irq_dispatch
 * performs EOI first and only then performs a voluntary yield in the
 * interrupted task's context. Bounded PREEMPT lines mark the first
 * three arms so a boot log proves the quantum path fired. */
void pit_irq(unsigned irq,const struct interrupt_frame *frame){
    (void)irq;(void)frame;ticks++;scheduler_tick();
    /* Pre-init safe: scheduler maps sched_cpu() to BSP/0 before init
     * and runnable<2 short-circuits the arm, so early ticks only cost
     * a counter decrement. */
    int was_set=scheduler_need_resched();
    scheduler_preempt_tick();
    if(!was_set&&scheduler_need_resched()){
        extern void kernel_log(const char *s);
        extern void kernel_log_dec(uint64_t v);
        if(preempt_lines<3){kernel_log("PREEMPT ");kernel_log_dec(preempt_lines+1);kernel_log("\r\n");preempt_lines++;}
    }
}
uint64_t pit_ticks(void){return ticks;}
