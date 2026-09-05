# RixuriOS Implementation Status

## Current position

RixuriOS is an x86_64/AMD64-only kernel moving from early kernel/device foundations into the first real userspace process milestone.

The repository contains real implementations for the early boot, memory, interrupt, scheduler/process, syscall, VFS/block, NVMe capability discovery, xHCI capability discovery, and initial ELF/address-space paths. These are implementation baselines, not blanket completion claims.

## Evidence rule

A subsystem is **COMPLETE** only after its required implementation, real execution, integration, negative/failure handling, regression coverage, security review, measured behavior, and documentation evidence exist. QEMU/build success alone is not hardware qualification.

Valid states include: `COMPLETE`, `IN PROGRESS`, `BLOCKED`, `NOT TESTED`, `UNSUPPORTED`, `DEGRADED`, and `FAIL`.

## Immediate focus

1. Make user address spaces safe with respect to kernel-owned mappings and page ownership.
2. Make ELF loading transactional and permission-correct.
3. Make process/scheduler activation and ring-3 entry robust.
4. Establish a real embedded/user ELF launch path.
5. Harden syscall/user-pointer validation and return paths.
6. Add negative/regression coverage before moving to later userspace phases.

## Not claimed as complete

Preemptive SMP scheduling, ring-3 execution, the complete syscall surface, IPC/signals/pipes/shared memory, full PCIe ECAM/DMA/IOMMU/MSI-X, complete NVMe I/O/recovery, RixFS, complete xHCI transfer machinery, HID, TTY/PTY/shell userspace, networking/RTL8125, libc/musl/POSIX/Linux compatibility, dynamic linking, init/services/package management, developer platform, audio/graphics, installer/recovery, physical-hardware qualification, pre-GUI certification, and GUI remain incomplete.

## Local validation

GitHub Actions is not used as the project's current validation path. The primary developer validation path is local build → UEFI image → QEMU serial boot → negative/regression tests → physical hardware qualification where required.

The current environment can inspect and modify repository sources but cannot honestly report a local QEMU execution result when executable/network access is unavailable. No test is marked PASS without actual evidence.
