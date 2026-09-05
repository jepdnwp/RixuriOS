# RixuriOS Architecture

RixuriOS is a 64-bit-only x86_64/AMD64 Unix-like operating system.

## Current layering

```text
UEFI
  -> ELF64 loader / boot handoff
  -> x86_64 kernel entry
  -> GDT / IDT / CPU primitives
  -> PMM / VMM / kernel heap
  -> APIC / IRQ infrastructure
  -> scheduler / processes / syscalls
  -> PCI / DMA / device model
  -> NVMe / xHCI / HID / network / audio / GPU
  -> VFS / RixFS
  -> init / devfs / procfs / sysfs
  -> libc / musl / POSIX / dynamic linking
  -> TTY / PTY
  -> Rixuri Shell / coreutils
  -> developer environment / Mayo
  -> graphics / Vulkan
  -> GUI (deferred)
```

## Kernel language boundary

The kernel uses freestanding C11/C17 and minimal x86_64 assembly. It must not depend on glibc, musl, POSIX, or Linux kernel APIs.

Userspace may use musl and Unix/POSIX compatibility layers. The syscall ABI is the explicit boundary between the kernel and userspace.

## Graphics boundary

Framebuffer/GOP and GPU infrastructure are kernel/device concerns. Vulkan is a first-class userspace graphics target with a loader/ICD boundary and real device capability reporting. GUI work is blocked until the pre-GUI operating-system gate is substantially complete.

## Development order

The authoritative detailed sequence is `docs/ROADMAP.md`. Do not skip unfinished lower layers merely to produce a visible demo. Real functionality and regression coverage take priority over appearance.
