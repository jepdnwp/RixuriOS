# RixuriOS Implementation Status

## Evidence rule
A subsystem is not marked COMPLETE merely because source exists. Completion requires correct implementation, real execution, integration, failure handling, regression coverage, security review, measured behavior, and documentation. Until those gates are observed, the state is `IMPLEMENTED / NOT YET VALIDATED` rather than PASS.

## Current implementation checkpoint

- Architecture: x86_64 / AMD64 only.
- Kernel: freestanding C17 + minimal x86_64 assembly.
- Build: `-Wall -Wextra -Werror`; ELF/header/program-header checks are part of the build.
- UEFI: boot handoff, memory-map capture, ELF64 validation/loading foundation.
- Memory: PMM/VMM/kernel heap and user address-space creation with explicit partial-allocation cleanup.
- Interrupts: GDT/IDT/exception/IRQ foundation, LAPIC/IOAPIC/PIT integration.
- ACPI: RSDP/SDT validation, XSDT/RSDT fallback, MADT LAPIC/x2APIC/IOAPIC/ISO parsing, MCFG parsing, FADT S5 extraction.
- Scheduling/processes: process/thread/task foundations, user task transition, wait/exit/signal primitives.
- Syscalls: user-pointer validation and filesystem-backed read/write/openat/close/stat integration.
- PCI/DMA/MSI-X: ECAM discovery, BAR sizing, DMA page allocation, MSI-X table programming. IOMMU remains explicitly unavailable until real DMAR/IVRS translation domains are implemented.
- Storage: block layer/cache and NVMe controller/namespace discovery with real read/write/flush path; timeout/recovery and concurrency remain qualification work.
- RixFS: persistent inode/extents, directory lookup/readdir, create/mkdir/unlink, explicit formatter, redo journal replay, read-only fsck.
- VFS: persistent root mount, hierarchical path traversal, per-process file descriptors, open/read/write/readdir/stat/mkdir/unlink.
- Time: CMOS RTC read in binary/BCD and 12/24-hour modes, Unix epoch conversion, PIT-backed monotonic clock, RTC-backed realtime clock.
- Power: hardware reboot path and ACPI S5 shutdown path when validated FADT/DSDT power data is available; otherwise shutdown returns unsupported rather than faking success.
- USB/xHCI: controller/capability foundation exists; full HID keyboard/mouse and hotplug qualification remain future phases.
- GUI: deliberately untouched; GUI remains last in the roadmap.

## Validation boundary

The repository has not been honestly certified by a real build/QEMU run in this execution environment. Hardware-specific claims therefore remain `NOT YET VALIDATED`. In particular, NVMe real I/O, xHCI completion, SMP/preemption stability, ACPI S5 behavior, and the historical UEFI `#UD` regression still require execution on the target test setup.

## Next phase

Phase 15: USB/xHCI protocol completion, device-slot lifecycle, endpoint/control-transfer correctness, HID keyboard/mouse reports, disconnect/hotplug handling, and negative-path tests.
