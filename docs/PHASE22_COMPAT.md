# Phase 22 — libc / POSIX / Compatibility Record

Status: software + host-test + QEMU static scope. Dynamic linking, full
musl, kernel threads and TLS belong to Phase 23 / the hardware track and
are recorded here as explicit DEFERRED items, not claims.

## 1. Layer invariant

```text
kernel syscall ABI (RIX_SYS_*, int $0x80)
  -> RixuriOS native libc (user/libc: unistd.h + libc.c/unistd.c)
    -> POSIX/Linux compatibility shims (same tree: sys/*, pthread.h, ...)
      -> application
```

The kernel never includes libc headers and never changes for a single
libc consumer; all adaptation lives in `user/libc`. Verified by
construction: `kernel/` includes only `include/` + its own headers.

## 2. Syscall coverage (ABI version 1)

| # | kernel | native wrapper | POSIX spelling | state |
|---|--------|----------------|----------------|-------|
| 0 | READ | read | read/ssize_t | working |
| 1 | WRITE | write | write | working |
| 2 | OPENAT | openat/open/creat | open/creat | working |
| 3 | CLOSE | close | close | working; also releases sockets (VFS first, then socket table) |
| 4 | STAT | stat | stat (rix_stat_t; no timestamps) | working |
| 7 | POLL | poll | poll | kernel ENOSYS, fails closed |
| 8 | LSEEK | lseek | lseek | working |
| 9/10/11 | MMAP/MPROTECT/MUNMAP | mmap/munmap/mprotect | same | kernel ENOSYS; heap path is brk/sbrk |
| 12 | BRK | brk/sbrk | — (malloc backend) | working |
| 13 | CLOCK_GETTIME | clock_gettime | clock_gettime(CLOCK_REALTIME/MONOTONIC) | working; both clocks share the realtime source |
| 16 | IOCTL | ioctl | ioctl | kernel ENOSYS |
| 22 | PIPE | pipe | pipe | working |
| 32/33 | DUP/DUP2 | dup/dup2 | dup/dup2 | working |
| 35 | NANOSLEEP | nanosleep/sleep/usleep | same | working |
| 39/140 | GETPID/GETPPID | getpid/getppid | same | working |
| 41–45 | SOCKET/BIND/CONNECT/SEND/RECV | socket_open/... + socket/bind/connect/sendto/... | POSIX socket API (AF_INET loopback; external where device stack serves) | working (loopback QEMU-proven); listen/accept/shutdown ENOSYS |
| 57/59/61/247 | FORK/EXECVE/WAIT/WAITPID | fork/execve/wait/waitpid | same; WNOHANG=1; WEXITSTATUS et al in sys/wait.h | working |
| 60 | EXIT | _exit/exit/_Exit | exit runs ≤16 atexit handlers LIFO, then _exit | working (QEMU-proven, status word checked) |
| 62/127/142 | KILL/SIGPENDING/SIGPROCMASK | kill/raise/sigpending/sigprocmask/pause | same; signal()/sigaction() declared but ENOSYS (no delivery ABI) | partial |
| 78–87 | GETDENTS/GETCWD/CHDIR/MKDIR/.../LINK | getdents/opendir/readdir/... | dirent + fcntl F_DUPFD/F_GETFL/F_SETFL | working; fstat/lstat ENOSYS; F_GETFD/F_SETFD reserved |
| 79/80 | GETCWD/CHDIR | getcwd/chdir | same + getopt/sysconf/getpagesize (pure) | working |
| 102–119 | credentials/ACL/caps/sessions | getuid/.../access/fcntl | access (stat-based), chmod/chown/rename/link | working within bounded model |
| 139 | GETRANDOM | getrandom/arc4random | getrandom | working where CPU has RDRAND/RDSEED else ENOSYS fallback |
| 141/143 | ISATTY/FCNTL | isatty/fcntl | same | working subset |

wait status word = raw 64-bit exit code (see `process_exit`); the shell
consumes `(int)status` directly and `sys/wait.h` documents the macros.

## 3. errno

Native codes match Linux numbers where they overlap (EPERM=1 … EINVAL=22,
ENOSYS=38, ETIMEDOUT=110). Socket-layer remap: raw -2/-3 → EAGAIN
(ARP-pending/empty queue — note: blocking receive is NOT implemented,
empty queue returns EAGAIN), -4 → EMSGSIZE, -110 → ETIMEDOUT.
`strerror`/`strerror_r` cover every defined code.

## 4. Header matrix

Working: assert, ctype, dirent, errno, fcntl (subset), limits, stdbool,
stddef, stdint (+SIZE_MAX), stdio (buffered FILE, printf/scanf subsets,
stdin/out/err, tmpfile), stdlib (malloc/calloc/realloc, strtol/ul,
qsort/bsearch, getenv/setenv/putenv/clearenv, rand/arc4random,
exit/atexit/abort-N/A-system-ENOSYS), string (+strnlen/strtok_r/memccpy),
signal (sets/mask/pending/raise/pause), time (+struct timespec,
clock_gettime/nanosleep POSIX signatures), unistd, sys/stat (+fstat/lstat
stubs), sys/types, sys/wait (macros), sys/time (gettimeofday),
netinet/in.h + arpa/inet.h (IPv4 only), sys/socket.h, pthread.h
(mutex/once local; create/join/detach ENOSYS), locale.h (C locale only),
wchar.h (strict UTF-8).

Fail-closed stubs (declared, ENOSYS): mmap/munmap/mprotect, poll, ioctl,
fstat/lstat, signal/sigaction, listen/accept/shutdown, system,
pthread_create/join/detach.

Reserved/missing by design: dynamic loading (dlopen — Phase 23),
`_Thread_local` (no PT_TLS/%fs until Phase 23), wide collation, timezones
(UTC only), pthreads across processes, O_CLOEXEC/nonblocking pipe
semantics, read()/write() on socket fds (use send/recv).

## 5. Porting notes (Linux/glibc divergences)

- `struct timespec` IS the ABI: identical layout to the kernel word;
  `rix_timespec_t` (sec/nsec fields) is the legacy spelling of the same
  16 bytes. New code uses tv_sec/tv_nsec.
- `exit()` does NOT flush application FILEs (no global stream registry):
  call `fflush()` first. Normal `_start` return bypasses `exit()` and
  therefore atexit handlers — call `exit()` explicitly when handlers
  matter. Verified by posix-test (handler byte through pipe + status 5).
- `pid_t` is 64-bit; printf it with `%ld`.
- `close()` spans two fd tables (VFS first, sockets second); socket fds
  may numerically overlap stdio. Never assume `socket()` returns >2.
- No `fork()`+threads mixing: mutexes are process-local spinlocks.
- `recv()` on an empty queue returns EAGAIN even on blocking sockets.
- `mmap`/`poll`/`ioctl`/`signal()`/`listen()` and friends compile, then
  fail closed at runtime; probe with ENOSYS checks, not `#ifdef`.
- Time is UTC-only; `CLOCK_MONOTONIC == CLOCK_REALTIME` source.
- `rand()` is deterministic LCG; `arc4random()` needs CPU
  RDRAND/RDSEED or it falls back deterministically (never use the
  fallback for keys — documented in source).

## 6. musl sysroot (bootstrap)

`make sysroot` (scripts/musl-sysroot.sh) assembles
`build/sysroot/{usr/include,usr/lib,usr/src}` from `user/libc` plus a
fresh `crt0.o` and an `ABI.txt` record (ABI version, arch, unsupported
list). This is the sysroot SHAPE the musl port will populate; the full
musl libc (startup relocations, dynamic linker, pthread over kernel
threads, regression suite against musl tests) requires the Phase 23
loader and is NOT claimed.

## 7. TLS / loader handoff (Phase 23 input)

- User objects build `-fno-pie -mcmodel=large`, linked by `user/init.ld`
  (no TLS segments emitted; `__thread`/`_Thread_local` use is a LINK
  error today — intentional, not silent).
- `user/programs/start.S` is the static startup object: argc/argv/envp
  marshalling + raw exit syscall. It becomes `crt0`/`Scrt1` material.
- Required Phase 23 work: PT_DYNAMIC/GOT/PLT relocation, DT_NEEDED
  loading, `%fs` setup + PT_TLS (initial-exec + dynamic models),
  `dlopen` authorization, THEN musl syscall-layer retarget + pthread
  over a new kernel thread/futex primitive.

## 8. Compatibility corpus (observed, QEMU NVMe image)

- Host: `make test` — libc_test (string/stdlib/stdio/env/time/scanf/
  locale/wchar/pthread-mutex/getopt/sysconf/inet/timeval), hosts_test,
  plus all kernel subsystem suites.
- Guest: `/usr/bin/posix-test` via scripts/qemu_posix_test.py — 27
  groups: clock/nanosleep/gettimeofday/sysconf, mmap-family/poll/ioctl
  ENOSYS, signal/sigaction ENOSYS + sigpending, socket errors, UDP
  loopback echo with source validation, 20× socket open/bind/close
  rebinding (close-fallback proof), empty-recv EAGAIN, exit+atexit
  through fork/pipe/wait with WEXITSTATUS, sscanf/getopt/inet/pthread.
- Regression neighbors re-run for the timespec migration + close
  change: phase19-extended (`date`), session suite, process-utils,
  signal suite, ISO boot (see VALIDATION_LOG / checkpoint entry).

## 9. Known limitations (remain open past Phase 22)

Dynamic linking/loader/TLS; kernel threads/futex/full pthread; signal
delivery frames; blocking socket receive; file-backed mmap; poll/ioctl
command sets; fd-stat/readlink/symlinks; timestamps in stat; non-C
locales/timezones; physical-hardware qualification (all QEMU evidence
is disposable-image class).
