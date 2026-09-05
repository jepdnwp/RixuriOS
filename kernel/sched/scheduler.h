#pragma once
#include <stdint.h>

typedef uint64_t rix_task_id_t;
typedef void (*rix_kernel_thread_fn)(void *arg);

int scheduler_init(void);
void scheduler_tick(void);
uint64_t scheduler_ticks(void);
int scheduler_create_kernel_thread(rix_kernel_thread_fn entry, void *arg, rix_task_id_t *out_id);
int scheduler_create_user_process(uint64_t pid, uint64_t entry, uint64_t user_stack, rix_task_id_t *out_id);
void scheduler_yield(void);
rix_task_id_t scheduler_current_id(void);
uint32_t scheduler_runnable_count(void);
