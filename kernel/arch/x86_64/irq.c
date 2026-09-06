#include "irq.h"
#include "apic.h"
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
    lapic_eoi();
    /* Interrupt entry frames are not task stacks. Timer IRQs only account time;
       voluntary yields perform context switches until a dedicated IRQ-return
       scheduler path is implemented. */
    if (irq == 0) scheduler_tick();
}
