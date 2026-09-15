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
#define RIX_SYS_CHMOD 90
#define RIX_SYS_CHOWN 91
#define RIX_SYS_RENAME 82
#define RIX_SYS_MKDIR 83
#define RIX_SYS_RMDIR 84
#define RIX_SYS_SYMLINK 85
#define RIX_SYS_UNLINK 87
#define RIX_SYS_READLINK 88
#define RIX_SYS_LINK 86
#define RIX_SYS_GETDENTS 78
#define RIX_SYS_LSEEK 8
#define RIX_SYS_POLL 7
#define RIX_SYS_MMAP 9
#define RIX_SYS_MPROTECT 10
#define RIX_SYS_MUNMAP 11
#define RIX_SYS_BRK 12
#define RIX_SYS_IOCTL 16
#define RIX_SYS_NANOSLEEP 35
/* Cooperative yield (Linux-compatible number). Additive ABI extension;
 * ABI version stays 1 (no breaking change). */
#define RIX_SYS_YIELD 24
#define RIX_SYS_CLOCK_GETTIME 13
#define RIX_SYS_GETCWD 79
#define RIX_SYS_CHDIR 80
#define RIX_SYS_GETUID 102
#define RIX_SYS_GETGID 104
#define RIX_SYS_SETUID 105
#define RIX_SYS_SETGID 106
#define RIX_SYS_GETGROUPS 115
#define RIX_SYS_SETGROUPS 116
#define RIX_SYS_GETACL 117
#define RIX_SYS_SETACL 118
#define RIX_SYS_CLEARACL 119
#define RIX_SYS_GETSID 120
#define RIX_SYS_SETSID 121
#define RIX_SYS_TTY_ATTACH 122
#define RIX_SYS_TTY_DETACH 123
#define RIX_SYS_SESSION_LOGIN 124
#define RIX_SYS_SESSION_LOGOUT 125
#define RIX_SYS_LIST_SESSIONS 126
#define RIX_SYS_GETCAP 132
#define RIX_SYS_DROPCAP 133
#define RIX_SYS_GETAUDITUID 135
#define RIX_SYS_SETAUDITUID 136
#define RIX_SYS_DELEGATECAP 137
#define RIX_SYS_LIST_PROCESSES 138
/* Phase R1 (Phase 06: PID/TID lifecycle): read-only thread-table
 * snapshot. Additive ABI extension; ABI version stays 1. Args mirror
 * LIST_PROCESSES: rdi=out array, rsi=capacity, rdx=count out. */
#define RIX_SYS_LIST_THREADS 144
/* waitpid(2): rdi=child (or -1 any), rsi=status out, rdx=options.
 * Header-owned since the F5 registry audit (was a syscall.c local). */
#define RIX_SYS_WAITPID 247
#define RIX_WAITPID_NOHANG 1u
#define RIX_SYS_GETRANDOM 139
#define RIX_SYS_GETPPID 140
#define RIX_SYS_ISATTY 141
#define RIX_SYS_SIGPROCMASK 142
#define RIX_SYS_FCNTL 143
#define RIX_SYS_STATFS 145
#define RIX_SYS_SYSINFO 146
#define RIX_SYS_KLOG_READ 147
#define RIX_SYSINFO_VERSION 1u
typedef struct { uint32_t version; uint32_t struct_size; uint32_t page_size; uint32_t flags; uint64_t total_pages; uint64_t free_pages; uint64_t reserved_pages; uint64_t uptime_sec; uint32_t uptime_nsec; uint32_t pad; uint64_t reserved0; } rix_sysinfo_t;
_Static_assert(sizeof(rix_sysinfo_t)==64,"sysinfo layout must stay 64 bytes");
#define RIX_SYS_GETPID 39
#define RIX_SYS_KILL 62
#define RIX_SYS_EXIT 60
#define RIX_SYS_WAIT 61
#define RIX_SYS_SOCKET 41
#define RIX_SYS_BIND 42
#define RIX_SYS_CONNECT 43
#define RIX_SYS_SEND 44
#define RIX_SYS_RECV 45
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
