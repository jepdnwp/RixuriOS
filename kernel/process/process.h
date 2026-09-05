#pragma once
#include <stddef.h>
#include <stdint.h>

#include "address_space.h"

typedef uint64_t pid_t;
typedef enum { RIX_PROC_UNUSED=0, RIX_PROC_RUNNING=1, RIX_PROC_SLEEPING=2, RIX_PROC_ZOMBIE=3 } rix_process_state_t;
#define RIX_PROCESS_MAX 128
#define RIX_PROCESS_NAME_MAX 32
#define RIX_PROCESS_FD_MAX 64

typedef struct {
    pid_t pid;
    pid_t parent;
    rix_process_state_t state;
    uint32_t uid;
    uint32_t gid;
    rix_address_space_t address_space;
    uint64_t kernel_stack;
    uint64_t kernel_stack_size;
    uint64_t exit_status;
    uint64_t fd_bitmap;
    char name[RIX_PROCESS_NAME_MAX];
} rix_process_t;

int process_init(void);
pid_t process_current(void);
rix_process_t *process_lookup(pid_t pid);
int process_create(const char *name, pid_t parent, pid_t *out_pid);
int process_create_user(const char *name, pid_t parent, const void *image, uint64_t image_size,
                        uint64_t *out_entry, uint64_t *out_user_stack);
int process_set_state(pid_t pid, rix_process_state_t state);
int process_exit(pid_t pid, uint64_t status);
size_t process_count(void);
