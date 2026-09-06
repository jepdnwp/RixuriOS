#include "pit.h"
#include "irq.h"
#include "../../sched/scheduler.h"
#include <stdint.h>
#define PIT_HZ 1193182u
static volatile uint64_t ticks;
static inline void outb(uint16_t p,uint8_t v){__asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p));}
int pit_init(uint32_t hz){if(!hz||hz>PIT_HZ)return -1;uint32_t div=PIT_HZ/hz;if(div<1)div=1;if(div>65535)div=65535;outb(0x43,0x36);outb(0x40,(uint8_t)div);outb(0x40,(uint8_t)(div>>8));ticks=0;return irq_register(0,pit_irq);}
void pit_irq(unsigned irq,const struct interrupt_frame *frame){(void)irq;(void)frame;ticks++;scheduler_tick();}
uint64_t pit_ticks(void){return ticks;}
