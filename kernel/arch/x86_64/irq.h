#pragma once

#include <stdint.h>

struct interrupt_frame;
typedef void (*irq_handler_t)(unsigned irq, const struct interrupt_frame *frame);

int irq_register(unsigned irq, irq_handler_t handler);
void irq_unregister(unsigned irq);
void x86_irq_dispatch(const struct interrupt_frame *frame);
