#pragma once
#include <stdint.h>
int pit_init(uint32_t hz);
uint64_t pit_ticks(void);
void pit_irq(unsigned irq, const void *frame);
