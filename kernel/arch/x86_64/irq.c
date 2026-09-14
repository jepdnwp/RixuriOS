#include "irq.h"
#include "apic.h"
#include "pic.h"
#include "../../sched/scheduler.h"
#include <stddef.h>

struct interrupt_frame { uint64_t vector; uint64_t error; uint64_t rip, cs, rflags, rsp, ss; };
static irq_handler_t handlers[16];

int irq_register(unsigned irq, irq_handler_t handler) {
    if (irq >= 16 || !handler || handlers[irq]) return -1;
    handlers[irq] = handler;
    return 0;
}

void irq_unregister(unsigned irq) {
    if (irq < 16) handlers[irq] = NULL;
}

void x86_irq_dispatch(const struct interrupt_frame *frame) {
    if (!frame || frame->vector < 32 || frame->vector > 47) return;
    unsigned irq = (unsigned)(frame->vector - 32);
    irq_handler_t handler = handlers[irq];
    if (handler) handler(irq, frame);
    /* EOI FIRST, then consider a voluntary yield. The APIC ISR bit must
     * be clear across any context switch (P1 lesson: a switch with the
     * ISR held wedges that vector on this LAPIC). The yield below runs
     * as a normal C call on the INTERRUPTED task's kernel stack: the
     * IRQ assembly stub returns through this same frame after we return,
     * and rix_context_switch saves this stack pointer into the task
     * slot — so the preempted task resumes here and irets normally.
     * PIT only ARMS the flag, and this yield only fires when preemption
     * is enabled, so allocator locks held by the interrupted task stay
     * consistent (they are irqsave: the PIT cannot be inside them). */
    if (pic_active()) { pic_eoi(irq); lapic_eoi(); } else lapic_eoi();
    if (scheduler_should_yield_from_irq()) scheduler_yield();
}
