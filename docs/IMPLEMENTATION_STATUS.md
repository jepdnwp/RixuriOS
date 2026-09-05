# RixuriOS Implementation Status

## Current position

RixuriOS is an x86_64/AMD64-only kernel moving from early kernel/device foundations through userspace and device-subsystem milestones.

The repository contains real implementations for early boot, memory, interrupt, scheduler/process, syscall, VFS/block, NVMe capability discovery, xHCI capability discovery, ELF/address-space loading, IPC channels, signals, shared-memory mapping, PCI ECAM discovery, DMA page allocation, PCI BAR sizing, and MSI-X capability discovery. These are implementation baselines, not blanket completion claims.

## Evidence rule

A subsystem is **COMPLETE** only after its required implementation, real execution, integration, negative/failure handling, regression coverage, security review, measured behavior, and documentation evidence exist. QEMU/build success alone is not hardware qualification.

Valid states include: `COMPLETE`, `IN PROGRESS`, `BLOCKED`, `NOT TESTED`, `UNSUPPORTED`, `DEGRADED`, and `FAIL`.

## Current implementation focus

- Phase 08: user address spaces and ELF64 launch path are implemented at foundation level; ring-3 execution still requires real boot/integration proof and stronger fault/return-path validation.
- Phase 09: process wait/reap, signal pending/masking, shared-memory map/unmap, and IRQ-safe IPC channels are implemented at foundation level. Blocking IPC, full pipe/file-descriptor objects, signal delivery frames, and complete IPC syscall surface remain.
- Phase 10: ACPI MCFG/ECAM discovery, PCI capability traversal, BAR sizing, DMA page allocation, and MSI-X table discovery are implemented at foundation level. IOMMU programming, safe MMIO lifecycle, interrupt-vector allocation/programming, and device-driver integration remain.
- Phase 11+: storage/network/filesystem/device-driver and userspace work continues in roadmap order.

## Not claimed as complete

Preemptive SMP scheduling, ring-3 execution qualification, the complete syscall surface, blocking IPC/pipes/full signal delivery, full PCIe DMA/IOMMU/MSI-X programming, complete NVMe I/O/recovery, RixFS, complete xHCI transfer machinery, HID, TTY/PTY/shell userspace, networking/RTL8125, libc/musl/POSIX/Linux compatibility, dynamic linking, init/services/package management, developer platform, audio/graphics, installer/recovery, physical-hardware qualification, pre-GUI certification, and GUI remain incomplete.

## Local validation

GitHub Actions is not used as the project's current validation path. The primary developer validation path is local build → UEFI image → QEMU serial boot → negative/regression tests → physical hardware qualification where required.

The current environment can inspect and modify repository sources but cannot honestly report a local QEMU execution result when executable/network access is unavailable. No test is marked PASS without actual evidence.
