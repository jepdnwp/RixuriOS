#include "scheduler.h"
#include "../arch/x86_64/irq.h"
#include <stddef.h>
static volatile uint64_t ticks;
static void sched_irq(unsigned irq,const void *frame){(void)irq;(void)frame;scheduler_tick();}
int scheduler_init(void){ticks=0;return irq_register(0,sched_irq);}
void scheduler_tick(void){ticks++;}
uint64_t scheduler_ticks(void){return ticks;}
