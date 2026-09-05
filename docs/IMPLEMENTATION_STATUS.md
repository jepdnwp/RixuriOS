# RixuriOS Implementation Status

This file is an engineering ledger, not a claim of release readiness.

## Current position

The repository is actively moving through the pre-userspace kernel/device stages. The following components now have real code on `main`:

- x86_64 UEFI handoff and kernel entry foundation;
- PMM/VMM/heap foundation;
- GDT/user segments/TSS load;
- IDT and exception/IRQ entry;
- LAPIC/ACPI/IOAPIC/PIT foundation;
- ACPI-aware IOAPIC polarity/trigger translation;
- interrupt-safe spinlocks and wait queues;
- cooperative kernel-thread context-switch primitive;
- process table/lifecycle foundation;
- PCI legacy configuration enumeration;
- secure user-pointer range/query/copy primitives;
- initial `int 0x80` syscall ABI with `getpid`, bounded `write`, and `exit` dispatch;
- VFS path normalization/root vnode foundation;
- block-device/BIO ABI;
- NVMe controller capability discovery;
- xHCI controller capability/operational register discovery.

## Not yet certified

None of the following are declared complete merely because their foundations exist:

- preemptive SMP scheduler;
- real ring-3 process execution;
- full syscall set;
- user address-space creation and ELF execution;
- IPC/signals/pipes;
- PCIe ECAM/DMA/IOMMU/MSI-X;
- NVMe admin queues/Identify/I/O/flush/recovery;
- RixFS on-disk implementation;
- xHCI rings/contexts/transfers;
- USB HID path;
- TTY/PTY/shell/userspace;
- network stack/RTL8125;
- libc/musl/POSIX/dynamic linker;
- init/services/package manager/developer platform;
- audio/GPU/Vulkan;
- installer/recovery/hardware qualification;
- Phase 34 certification;
- Phase 35 GUI.

## Evidence rule

A status becomes `COMPLETE` only after the applicable build, execution, integration, negative/failure, regression, security, performance and documentation evidence exists. Hardware-only claims require physical-hardware logs. QEMU-only evidence is never promoted to a physical-hardware checkpoint.
