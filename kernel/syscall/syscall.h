#pragma once
#include <stdint.h>
#define RIX_SYS_READ 0
#define RIX_SYS_WRITE 1
#define RIX_SYS_OPENAT 2
#define RIX_SYS_CLOSE 3
#define RIX_SYS_PIPE 22
#define RIX_SYS_CLOSE_PIPES_EXCEPT 248
#define RIX_SYS_DUP 32
#define RIX_SYS_DUP2 33
#define RIX_SYS_SPAWN 134
#define RIX_SYS_FORK 57
#define RIX_SYS_EXECVE 59
#define RIX_SYS_STAT 4
#define RIX_SYS_MKDIR 83
#define RIX_SYS_RMDIR 84
#define RIX_SYS_UNLINK 87
#define RIX_SYS_LINK 86
#define RIX_SYS_GETDENTS 78
#define RIX_SYS_POLL 7
#define RIX_SYS_MMAP 9
#define RIX_SYS_MPROTECT 10
#define RIX_SYS_MUNMAP 11
#define RIX_SYS_IOCTL 16
#define RIX_SYS_NANOSLEEP 35
#define RIX_SYS_CLOCK_GETTIME 13
#define RIX_SYS_GETCWD 79
#define RIX_SYS_CHDIR 80
#define RIX_SYS_GETUID 102
#define RIX_SYS_GETGID 104
#define RIX_SYS_SETUID 105
#define RIX_SYS_SETGID 106
#define RIX_SYS_GETPID 39
#define RIX_SYS_KILL 62
#define RIX_SYS_EXIT 60
#define RIX_SYS_WAIT 61
#define RIX_SYS_SIGMASK 14
#define RIX_SYS_SIGPENDING 127
#define RIX_SYS_SHM_CREATE 128
#define RIX_SYS_SHM_MAP 129
#define RIX_SYS_SHM_UNMAP 130
#define RIX_SYS_SHM_DESTROY 131
#define RIX_SYSCALL_ABI_VERSION 1u
typedef struct {uint64_t r15,r14,r13,r12,r11,r10,r9,r8;uint64_t rbp,rdi,rsi,rdx,rcx,rbx,rax;uint64_t vector,error;uint64_t rip,cs,rflags,rsp,ss;} rix_syscall_frame_t;
void syscall_init(void);
void syscall_dispatch(rix_syscall_frame_t *frame);
