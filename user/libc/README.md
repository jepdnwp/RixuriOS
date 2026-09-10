# RixuriOS libc

Freestanding bootstrap libc + POSIX/Linux compatibility layer over the
documented RixuriOS syscall ABI. The kernel never includes these headers
and never links this code.

```text
kernel ABI (RIX_SYS_*, int $0x80)
  -> native libc (unistd.h, libc.c + unistd.c)
    -> POSIX shims (sys/*, pthread.h, locale.h, wchar.h, netinet, arpa, poll.h)
      -> application
```

- Native layer: `unistd.h` (fd/process/socket/credential syscalls),
  `libc.c` (string/malloc/stdio/stdlib/time/env/locale/wchar), `unistd.c`
  (syscall wrappers + POSIX socket/mman/poll/ioctl/signal-clock surface).
- Compatibility: see `docs/PHASE22_COMPAT.md` for the syscall table,
  errno mapping, header matrix, porting notes and deferred list.
- Bootstrap sysroot: `make sysroot` assembles `build/sysroot` from this
  tree (musl fills it in Phase 23 once the dynamic loader exists).
- Tests: `tests/libc_test.c` (host, pure functions) and
  `user/programs/posix-test.c` + `scripts/qemu_posix_test.py` (guest,
  syscall-backed surface including UDP loopback and exit/atexit).
- Not here (fail closed or link-gated): dynamic loading, TLS, kernel
  threads, signal delivery, file-backed mmap, ioctl/poll command sets.
