#pragma once
#include <stdint.h>
int scheduler_init(void);
void scheduler_tick(void);
uint64_t scheduler_ticks(void);
