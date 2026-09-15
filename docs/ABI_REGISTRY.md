# RixuriOS Syscall ABI Registry

**Version:** 1 (`RIX_SYSCALL_ABI_VERSION 1u`, `kernel/syscall/syscall.h`).
**Entry:** `int 0x80`, `rax` = number. Frame registers follow System V
(`rdi rsi rdx r10 r8 r9`); unknown numbers fail closed with `-ENOSYS`.
Generated from source 2026-09-15; the header is normative, this file
descriptive — any drift between them is a bug. `STATFS 145` / `SYSINFO 146` / `KLOG_READ 147` / `FSTAT 148` / `LSTAT 149` are v1 additive
(precedents: `YIELD 24`, `LIST_THREADS 144`).

## Version policy

- v1 is additive only: new numbers may be allocated, existing numbers
  never change meaning. Additive extensions keep the version at 1
  (precedents: `YIELD 24`, `LIST_THREADS 144`).
- Numbers without a dispatch case return `-ENOSYS` (reserved for a
  later phase, never silently accepted).
- `WAITPID 247` moved into the header during the F5 audit (was a
  `syscall.c` local); no behavioral change.

## Table (number, name, status)

| # | Name | Status |
|---|---|---|
| 0 | READ | served |
| 1 | WRITE | served |
| 2 | OPENAT | served |
| 3 | CLOSE | served |
| 4 | STAT | served |
| 7 | POLL | served (immediate socket readiness; timeout blocking deferred) |
| 8 | LSEEK | served |
| 9 | MMAP | reserved (`-ENOSYS`) |
| 10 | MPROTECT | reserved (`-ENOSYS`) |
| 11 | MUNMAP | reserved (`-ENOSYS`) |
| 12 | BRK | served |
| 13 | CLOCK_GETTIME | served |
| 14 | SIGMASK | served |
| 16 | IOCTL | reserved (`-ENOSYS`) |
| 22 | PIPE | served |
| 24 | YIELD | served (v1 additive) |
| 32 | DUP | served |
| 33 | DUP2 | served |
| 35 | NANOSLEEP | served |
| 39 | GETPID | served |
| 41 | SOCKET | served |
| 42 | BIND | served |
| 43 | CONNECT | served |
| 44 | SEND | served |
| 45 | RECV | served |
| 57 | FORK | served |
| 59 | EXECVE | served |
| 60 | EXIT | served |
| 61 | WAIT | served |
| 62 | KILL | served |
| 78 | GETDENTS | served |
| 79 | GETCWD | served |
| 80 | CHDIR | served |
| 82 | RENAME | served |
| 83 | MKDIR | served |
| 84 | RMDIR | served |
| 85 | SYMLINK | served |
| 86 | LINK | served |
| 87 | UNLINK | served |
| 88 | READLINK | served |
| 90 | CHMOD | served |
| 91 | CHOWN | served |
| 102 | GETUID | served |
| 104 | GETGID | served |
| 105 | SETUID | served |
| 106 | SETGID | served |
| 115 | GETGROUPS | served |
| 116 | SETGROUPS | served |
| 117 | GETACL | served |
| 118 | SETACL | served |
| 119 | CLEARACL | served |
| 120 | GETSID | served |
| 121 | SETSID | served |
| 122 | TTY_ATTACH | served |
| 123 | TTY_DETACH | served |
| 124 | SESSION_LOGIN | served |
| 125 | SESSION_LOGOUT | served |
| 126 | LIST_SESSIONS | served |
| 127 | SIGPENDING | served |
| 128 | SHM_CREATE | served |
| 129 | SHM_MAP | served |
| 130 | SHM_UNMAP | served |
| 131 | SHM_DESTROY | served |
| 132 | GETCAP | served |
| 133 | DROPCAP | served |
| 134 | SPAWN | served |
| 135 | GETAUDITUID | served |
| 136 | SETAUDITUID | served |
| 137 | DELEGATECAP | served |
| 138 | LIST_PROCESSES | served |
| 139 | GETRANDOM | served |
| 140 | GETPPID | served |
| 141 | ISATTY | served |
| 142 | SIGPROCMASK | served |
| 143 | FCNTL | served |
| 144 | LIST_THREADS | served (v1 additive) |
| 145 | STATFS | served (v1 additive) |
| 146 | SYSINFO | served (v1 additive) |
| 147 | KLOG_READ | served (v1 additive) |
| 148 | FSTAT | served (v1 additive) |
| 149 | LSTAT | served (v1 additive) |
| 150 | ACCESS | served (v1 additive) |
| 151 | SHUTDOWN | served (v1 additive) |
| 152 | SETSOCKOPT | served (v1 additive) |
| 153 | GETSOCKOPT | served (v1 additive) |
| 154 | LISTEN | served (v1 additive) |
| 155 | ACCEPT | served (v1 additive; empty queue returns EAGAIN) |
| 247 | WAITPID | served (`NOHANG=1`) |
| 248 | CLOSE_PIPES_EXCEPT | served |

## Fault contract (Phase F1/F2)

- Bad user pointers fail with `-EFAULT` (validate-then-deref fast
  path; fixup-backed raw copies cover races to unmap).
- Faults in userspace (`cs==0x1b`, vectors 0,1,3,4,5,6,13,14,16,17,19)
  terminate the faulting process with status 139; all other faults
  freeze with forensics (see `kernel/arch/x86_64/idt.c` matrix).
- Negative tests: `abi-negative` (bad-arg rejections), `crashtest`
  (fault kill + reap), `fuzztest` (6000-call seeded blast).
