#pragma once
#include <stdint.h>
struct interrupt_frame;
int pit_init(uint32_t hz);
uint64_t pit_ticks(void);
void pit_irq(unsigned irq, const struct interrupt_frame *frame);
