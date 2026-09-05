# RixuriOS Architecture

## Target

- AMD64 / x86_64, 64-bit only.
- Kernel: freestanding C17 with minimal x86_64 assembly.
- Boot: UEFI firmware -> UEFI loader -> ELF64 kernel.
- Userspace target: musl + POSIX APIs with an explicit Linux/glibc compatibility layer where useful.

## Implementation order

1. UEFI boot and ELF64 handoff
2. GDT/IDT, exceptions and APIC
3. Physical/virtual memory and SMP
4. Scheduler, processes, threads and syscall ABI
5. ELF64 userspace and initial process
6. VFS/RixFS/block storage/NVMe
7. xHCI/HID/TTY/networking/RTL8125
8. libc/musl/POSIX/Linux compatibility
9. shell/coreutils/users/auth/Mayo
10. installer, build toolchain, QEMU and real-hardware regression
11. performance, security, release gates and final acceptance

All stages must remain buildable and must not silently replace required interfaces with stubs. Unsupported hardware or features must be reported explicitly.
