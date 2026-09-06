#pragma once
#include <stddef.h>
#include <stdint.h>
#include "address_space.h"

typedef uint64_t pid_t;
typedef enum { RIX_PROC_UNUSED=0, RIX_PROC_RUNNING=1, RIX_PROC_SLEEPING=2, RIX_PROC_ZOMBIE=3 } rix_process_state_t;
#define RIX_PROCESS_MAX 128
#define RIX_PROCESS_NAME_MAX 32
#define RIX_PROCESS_FD_MAX 64
#define RIX_SIGNAL_MAX 64
#define RIX_PROCESS_ARG_MAX 16
#define RIX_PROCESS_ARG_TEXT_MAX 128
#define RIX_PROCESS_CWD_MAX 256

typedef struct { pid_t pid; pid_t parent; pid_t process_group; pid_t session; rix_process_state_t state; uint32_t uid; uint32_t gid; rix_address_space_t address_space; uint64_t kernel_stack; uint64_t kernel_stack_size; uint64_t exit_status; uint64_t fd_bitmap; uint64_t signal_pending; uint64_t signal_mask; char name[RIX_PROCESS_NAME_MAX]; char cwd[RIX_PROCESS_CWD_MAX]; } rix_process_t;
int process_init(void);
pid_t process_current(void);
rix_process_t *process_lookup(pid_t pid);
int process_create(const char *name,pid_t parent,pid_t*out_pid);
int process_create_user(const char *name,pid_t parent,const void *image,uint64_t image_size,pid_t*out_pid,uint64_t*out_entry,uint64_t*out_user_stack);
int process_fork(pid_t parent,uint64_t user_rip,uint64_t user_rsp,pid_t *out_pid);
int process_exec_user(pid_t pid,const void*image,uint64_t image_size,uint64_t*out_entry,uint64_t*out_user_stack);
int process_exec_user_with_args(pid_t pid,const void *image,uint64_t image_size,
                                const char *const *argv, size_t argc,
                                const char *const *envp, size_t envc,
                                uint64_t *out_entry, uint64_t *out_user_stack);
int process_activate(pid_t pid);
int process_set_state(pid_t pid,rix_process_state_t state);
int process_exit(pid_t pid,uint64_t status);
int process_wait(pid_t parent,pid_t wanted,uint64_t *status,pid_t *child_pid);
int process_set_group(pid_t pid, pid_t process_group);
int process_set_session(pid_t pid, pid_t session);
int process_signal_group(pid_t process_group, unsigned signal);
size_t process_count(void);
int process_getcwd(pid_t pid, char *out, size_t capacity);
int process_setcwd(pid_t pid,const char *path);
uint32_t process_uid(pid_t pid);
uint32_t process_gid(pid_t pid);
int process_setuid(pid_t pid,uint32_t uid);
int process_setgid(pid_t pid,uint32_t gid);
