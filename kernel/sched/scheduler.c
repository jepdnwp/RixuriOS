#include "scheduler.h"
#include <stdint.h>
static volatile uint64_t ticks;
int scheduler_init(void){ticks=0;return 0;}
void scheduler_tick(void){ticks++;}
uint64_t scheduler_ticks(void){return ticks;}
