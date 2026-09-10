# RixuriOS Master Development Roadmap — v7

**Architecture:** x86_64 / AMD64 only  
**Kernel:** freestanding C11/C17 + minimal x86_64 assembly  
**Userspace:** Unix-like, musl/POSIX-oriented, dynamically linked  
**Product:** **single-user desktop PC operating system**  
**Operating target:** real AMD/Intel x86_64 desktop PCs first; QEMU is a development and regression platform, never a substitute for physical hardware  
**Product principle:** terminal-first, hardware-real, recovery-first  
**GUI:** **PHASE 100 ONLY — absolutely last**

> This is the single authoritative RixuriOS roadmap. All roadmap work belongs in this file. A phase is an engineering gate, not a wishlist checkbox. The roadmap is intentionally optimized for one real human using one desktop PC, rather than for enterprise multi-user, server, cloud, cluster or laptop features.

---

## 1. Product definition

RixuriOS is a technically coherent, reliable, recoverable **single-user x86_64 desktop PC OS**. The primary success criterion is that one owner can boot it on supported physical hardware, log into the machine, use a terminal, manage files and processes, use storage and networking, run real dynamically linked software, recover from failures, update the system and eventually use a graphical desktop.

The system still has kernel-level credentials, ownership and permission machinery because those boundaries are useful for correctness and security. However, RixuriOS does **not** optimize for multiple independent human accounts, enterprise directory services, multi-seat desktops, server tenancy, containers, cluster orchestration or cloud infrastructure.

### Explicitly out of primary scope

- Multiple simultaneous human desktop sessions.
- Enterprise identity systems such as LDAP/Active Directory.
- Multi-seat graphical login infrastructure.
- Server clustering and distributed consensus.
- Containers/VM orchestration.
- Laptop-only battery/lid UX and laptop power-profile management.
- Mobile-phone hardware support.
- Non-x86_64 architectures.
- A Linux compatibility layer as a product requirement.

These may be researched later, but they must never delay the core single-user desktop OS.

### Desktop-PC platform policy

ACPI is used for firmware discovery, MADT/interrupt routing, MCFG/PCIe discovery, timers, reboot/poweroff, thermal safety and normal desktop device initialization. Suspend/resume is optional and only justified by a concrete supported desktop target.

### GUI policy

Phases before 100 may implement framebuffer, display, GPU, DRM-like abstractions, acceleration preparation and graphics diagnostics. They may **not** be used to build the actual desktop product. No graphical shell, desktop environment, graphical settings application, display manager or GUI-first workflow is considered complete before Phase 100.

---

# 2. Non-negotiable engineering rules

1. x86_64 only.
2. Kernel remains freestanding; it never links against glibc, musl or POSIX.
3. Userspace consumes a documented RixuriOS syscall ABI.
4. Every subsystem documents ownership, lifetime, locking, execution context and error semantics.
5. Detection is not driver completion.
6. QEMU evidence and physical-hardware evidence are separate.
7. No fake hardware, fake packets, fake disk data or synthetic PASS records.
8. `PASS` requires reproducible evidence.
9. Unknown/corrupt media is rejected safely and is never silently formatted.
10. Destructive tests use disposable targets and explicit confirmation.
11. Positive, negative, boundary, timeout and recovery tests are required according to risk.
12. Security is reviewed at privilege, parser, DMA, filesystem, IPC and device boundaries.
13. Every historical failure becomes a regression test.
14. Performance cannot hide correctness defects.
15. Diagnostics, documentation and reproducibility are implementation work.
16. `SKIP`, `ENOSYS`, `NOT TESTED`, `DEGRADED`, `BLOCKED`, `FAIL` and `UNSUPPORTED` are explicit states; none means COMPLETE.
17. A demo implementation can never be promoted to PASS without replacing it with the real path.
18. ABI changes require an impact review and compatibility record.
19. Hardware support is per-device/per-platform and evidence-backed.
20. GUI cannot consume capacity while pre-GUI release gates are open.
21. One-person product decisions must favor simplicity, recoverability and debuggability over unnecessary generality.
22. A feature is complete only when its failure behavior is at least as deliberate as its success path.
23. Reboots, power loss, device removal and malformed input are first-class test cases.
24. No hidden Linux-specific behavior may be required by a core RixuriOS userspace program.
25. The owner must always have a documented recovery path from a broken userspace or failed update.

---

# 3. Universal phase workflow

`SPEC → ABI/DATA MODEL → DESIGN → IMPLEMENT → BUILD → UNIT → NEGATIVE → QEMU → INTEGRATION → HARDWARE → REGRESSION → SECURITY → PERFORMANCE → DOCUMENT → CHECKPOINT`

Every phase records: changed files, ABI changes, data formats, ownership rules, locking/context rules, error codes, timeout rules, recovery behavior, hardware assumptions, test commands, observations, evidence locations and unresolved limitations.

## Checkpoint vocabulary

`CP0 SPEC` · `CP1 BUILD` · `CP2 UNIT` · `CP3 BOOT` · `CP4 INTEGRATION` · `CP5 HARDWARE` · `CP6 REGRESSION` · `CP7 SECURITY` · `CP8 PERFORMANCE` · `CP9 DOCS` · `CP10 RELEASE`

---

# FOUNDATION — PHASES 00–09

## PHASE 00 — Governance, Source of Truth and Reproducible Build

- Canonical source tree and generated-file policy.
- Host/cross-toolchain separation and version pinning.
- Deterministic compiler/linker flags and image generation.
- Debug/release profiles, symbols, maps and provenance.
- Clean-host and offline build procedures.
- CI build, unit-test and artifact retention policy.
- ABI/versioning policy, architecture decision records and changelog.
- Checkpoint ledger, hardware inventory and known-failure register.
- Release-blocker classification and reproducible evidence format.

**Gate:** a clean environment can reproduce a traceable RixuriOS image.

## PHASE 01 — UEFI Boot and Firmware Handoff

- EFI table/function-pointer layouts and calling conventions.
- ELF64 validation with overflow, alignment and canonical-address checks.
- PT_LOAD allocation/copy/zero-fill and kernel entry contract.
- ACPI RSDP, GOP and firmware memory-map discovery.
- `ExitBootServices()` retry handling.
- Boot handoff versioning and firmware quirk reporting.
- Early crash diagnostics that preserve the last useful boot state.

**Gate:** real UEFI boot reaches the kernel with validated firmware state.

## PHASE 02 — CPU Bring-up, PMM, VMM and Kernel Memory

- CPUID/MSR wrappers and CPU feature policy.
- NX/WP/SMEP/SMAP capability detection and staged enforcement.
- Physical memory descriptors, reserved ranges and frame ownership.
- DMA-capable allocation, alignment and reference accounting.
- 4/5-level paging policy and permission transitions.
- Page-fault diagnostics and safe user-fault termination.
- Real kernel heap allocation/free, coalescing/slabs and leak diagnostics.
- Guard/debug allocation modes and corruption detection.

**Gate:** kernel memory can be allocated and genuinely reclaimed.

## PHASE 03 — GDT, TSS, IDT, Exceptions and Interrupt Entry

- GDT kernel/user segments and TSS.
- IST stacks for critical exceptions.
- Stable trap-frame ABI and register preservation.
- Exceptions 0–31 with actionable diagnostics.
- IRQ entry/return and nesting rules.
- EOI ownership and spurious IRQ handling.
- Interrupt-safe logging and fault-to-process policy.
- Malformed return-state and nested-fault tests.

## PHASE 04 — ACPI, LAPIC, IOAPIC, Timers and Initial SMP

- RSDP/XSDT/RSDT checksum and table validation.
- MADT CPU/LAPIC/IOAPIC entries and interrupt overrides.
- IOAPIC polarity/trigger configuration.
- MSI/MSI-X groundwork.
- LAPIC/x2APIC policy and IPI routing.
- AP trampoline and per-CPU state.
- APIC timer, HPET/PIT compatibility and monotonic time.
- TLB shootdown protocol and cross-CPU rendezvous.

**Gate:** at least two real CPU execution contexts operate safely without single-CPU assumptions.

## PHASE 05 — Synchronization, Wait Queues and Workqueues

- Spinlocks and IRQ-save locks.
- Mutexes, RW locks and semaphores.
- Wait queues and sleep/wakeup ownership.
- Atomic references and object lifetime rules.
- Lock ordering and deadlock diagnostics.
- Sleepable/non-sleepable context annotations.
- Kernel workers and deferred interrupt work.
- Cancellation, shutdown and worker-drain semantics.
- Contention tracing and stress tests.

## PHASE 06 — Process, Thread and Preemptive Scheduler Core

- PID/TID allocation and reuse protection.
- Process objects, parent/child state and zombies.
- Kernel threads and user-thread CPU contexts.
- Kernel stacks and thread-local architecture.
- Timer-driven preemption and per-CPU run queues.
- Priority/fairness, sleep/wakeup and idle threads.
- SMP load balancing and affinity.
- Starvation detection and scheduler accounting.
- Context-switch and latency instrumentation.

**Gate:** a real userspace workload can be preempted, sleep, wake and safely run on multiple CPUs.

## PHASE 07 — Syscall ABI and User/Kernel Boundary

- Stable syscall numbering/versioning.
- Syscall entry/return and kernel-stack transition.
- Canonical-address and range validation.
- Fault-safe copy-in/copy-out.
- FD/object validation and reference acquisition.
- Errno mapping and restart policy.
- Syscall tracing and ABI conformance tests.
- Malformed pointer, oversized argument and race-oriented tests.

## PHASE 08 — User Address Spaces and ELF64 Execution

- Independent user page tables.
- Kernel/user split and permission enforcement.
- ELF header/program-header validation.
- PT_LOAD mapping and BSS zeroing.
- User stack, guard page and ABI alignment.
- `argc/argv/envp/auxv` construction.
- PIE/non-PIE policy and ASLR architecture.
- `exec` replacement and complete address-space teardown.
- User page-fault isolation.

**Gate:** a genuine ring-3 process runs independently of the kernel address space.

## PHASE 09 — IPC, Pipes, Signals, Events and Shared Memory

- Anonymous pipes and FIFOs.
- Event/wait objects and pollable IPC.
- Shared memory with explicit permissions/lifetime.
- Process groups and signal state.
- Signal delivery and return-frame design.
- Unix-domain socket architecture and FD passing.
- IPC reference counting and process-death cleanup.
- Cancellation and partial read/write semantics.

---

# HARDWARE CORE — PHASES 10–21

## PHASE 10 — PCIe, MCFG, MMIO, DMA and Device Model

- PCI/PCIe configuration and ECAM/MCFG discovery.
- Capability parsing with loop/length validation.
- BAR sizing, MMIO mapping and resource ownership.
- Bus-master/DMA mapping API and cache coherency.
- MSI/MSI-X allocation and ownership.
- Bus/device/driver object model.
- Probe/remove/reset lifecycle.
- Bridge traversal and multifunction devices.
- Hotplug groundwork.
- IOMMU abstraction.

## PHASE 11 — Storage Core and Block Layer

- Stable block-device identity.
- BIO/request objects and scatter-gather.
- Queue depth and ordering.
- Read/write/flush/FUA/barrier semantics.
- Completion/cancellation ownership.
- Timeout, retry and reset policy.
- Page/buffer cache and dirty/writeback lifecycle.
- Direct-I/O coherence.
- Storage error classes and recovery states.

## PHASE 12 — Real NVMe Driver

- Controller reset/enable and capability validation.
- Admin queues and Identify Controller/Namespace.
- I/O queues and phase tags.
- PRP/SGL DMA construction.
- Polling and interrupt completion.
- Real read/write/flush.
- Namespace lifecycle.
- Timeout, abort and controller-reset recovery.
- Block-layer error translation.
- QEMU plus physical NVMe evidence.

**Detection, BAR mapping or Identify alone never closes this phase.**

## PHASE 13 — VFS and RixFS

- Vnode/inode/dentry/superblock/file abstractions.
- Mount tree and path resolution.
- Directory lookup/create/unlink/mkdir/rmdir.
- Open-file-description and FD semantics.
- Permissions and credential checks.
- Symlink/hard-link design and safe traversal.
- RixFS superblock, inode, extent, directory and free-space formats.
- Versioned on-disk specification.
- Journal/checksum/orphan recovery where required.
- fsck and read-only emergency mount.

## PHASE 14 — Time, RTC and Desktop-PC Platform Management

- Monotonic and realtime clocks.
- RTC/CMOS abstraction.
- Timer/sleep APIs and timeout monotonicity.
- Desktop-relevant ACPI operations.
- Reboot/poweroff/reset fallback paths.
- CPU idle states.
- Thermal-safety hooks.
- Suspend/resume only if a supported desktop target actually needs it.

**Excluded:** battery UX, lid UX and laptop-specific power profiles.

## PHASE 15 — USB/xHCI Core

- Capability/operational/runtime register model.
- DCBAA and scratchpads.
- Command/transfer/event rings.
- TRB cycle ownership.
- Slot/device/input-context lifecycle.
- Port reset, Address Device and Configure Endpoint.
- Control/bulk/interrupt transfer engines.
- Interrupters/MSI/MSI-X and DMA ordering.
- Timeout/controller reset/recovery.
- Hotplug/disconnect races.
- Historical xHCI regressions remain permanent tests.

## PHASE 16 — USB HID, Keyboard, Mouse and Input

- HID descriptor/report parsing with bounds checks.
- Boot/report protocol support.
- Interrupt-IN lifecycle.
- Keyboard modifiers, press/release/repeat and rollover.
- Mouse buttons, motion and wheel.
- Timestamped input event ABI.
- Disconnect cleanup and hotplug.
- xHCI → USB → HID → input → TTY integration.

**Synthetic input cannot close the physical HID gate.**

## PHASE 17 — TTY, PTY, Console and Terminal Engine

- TTY/PTY master/slave objects.
- Canonical/raw modes and termios-like controls.
- Input/output queues and flow control.
- UTF-8 and ANSI/VT parser.
- Terminal dimensions and resize events.
- Controlling terminal/session/process-group integration.
- Parser fuzzing and malformed escape handling.
- Console recovery when userspace terminal components fail.

## PHASE 18 — Shell, Job Control and Command Execution

- Parser, quoting and escaping.
- Parameter/environment expansion.
- Globbing.
- Pipelines and redirections.
- Safe environment inheritance.
- Command lookup.
- Foreground/background jobs.
- Process groups and signal-aware job control.
- Command substitution and builtins.
- Exit-status propagation.

**Gate:** the owner can genuinely operate the OS from its public terminal interface.

## PHASE 19 — Unix Utilities and Base Userland

- Filesystem utilities: `cat`, `cp`, `mv`, `rm`, `mkdir`, `rmdir`, `ls`, `find`.
- Text tools: `grep`, `sort`, `head`, `tail`, `printf`, `echo`.
- Process tools: `ps`, `kill`, `env`, `pwd`.
- Storage tools: `mount`, `umount`, `df`, `du`.
- Hardware and kernel diagnostics.
- Network inspection/configuration tools.
- Correct stderr, exit status and signal behavior.
- No Linux-private dependency hidden inside core utilities.

## PHASE 20 — Single-User Identity, Credentials and Local Security

- One primary human user model.
- UID/GID primitives retained for Unix compatibility and file ownership.
- `root`/kernel-privileged execution boundary.
- Credential inheritance and replacement.
- File permission checks.
- Optional ACL architecture only where justified.
- Login/session ownership and controlling terminal.
- Credential lifetime/revocation.
- Audit/security events.
- W^X, ASLR and stack hardening integration.

**Product simplification:** do not build enterprise account-management machinery merely because Unix has it.

## PHASE 21 — Networking Stack and Real Device Integration

- Ethernet interface abstraction.
- ARP, IPv4, ICMP and UDP.
- TCP ordering, ACKs, retransmission, windows and teardown.
- Socket API with blocking/nonblocking semantics.
- Routing-table architecture.
- DNS resolver.
- E1000/QEMU reference driver.
- RTL8125 RX/TX, DMA rings, interrupt path and reset/recovery.
- Physical networking evidence separated from loopback/guest evidence.

---

# USERSPACE CORE — PHASES 22–35

## PHASE 22 — libc, POSIX Surface and Native C Runtime

- C headers and ABI types.
- errno and error propagation.
- strings/memory/stdio/formatting.
- malloc/calloc/realloc/free wrappers.
- Environment handling.
- Directory/time/process/filesystem wrappers.
- Socket and signal wrappers.
- pthread API preparation.
- `getopt`, `sysconf`, `getpagesize` and selected useful APIs.
- Honest compatibility matrix.

**Gate:** static C/POSIX userspace surface is real and documented.

## PHASE 23 — Dynamic ELF Loader, Shared Libraries and TLS

- PT_INTERP/PT_DYNAMIC validation.
- DT_NEEDED/SONAME dependency graph.
- REL/RELA relocations.
- Symbol tables and hash tables.
- GOT/PLT.
- PIE and secure library search paths.
- Constructors/destructors.
- `dlopen`, `dlsym`, `dlclose`, `dlerror`.
- PT_TLS and `%fs` thread pointer.
- Static/dynamic TLS models.
- Auxiliary-vector completeness.
- Malformed ELF/shared-object fuzz corpus.

**Gate:** a real dynamically linked program runs without a test-only loader.

## PHASE 24 — Virtual Memory Mapping and mmap Family

- `mmap`/`munmap`/`mprotect`/`msync` policy.
- User virtual-address allocator.
- Anonymous mappings.
- File-backed mappings.
- Guard regions.
- Mapping overlap and unmap splitting.
- Page-fault-driven population.
- Copy-on-write design and reference accounting.
- OOM behavior and deterministic failure.
- Mapping exhaustion tests.

## PHASE 25 — Threads, Futexes and Thread Lifecycle

- `clone`/thread creation model as required by the native ABI.
- Kernel thread objects.
- User stack creation and cleanup.
- TLS initialization and `%fs` ownership.
- Futex wait/wake and timeout semantics.
- Mutex/condition-variable primitives.
- Thread exit/join/detach.
- Robust cleanup after abnormal termination.
- Race and contention tests.

**Gate:** a real multithreaded userspace program runs correctly on SMP.

## PHASE 26 — Signals and Asynchronous Process Control

- Signal numbers and dispositions.
- Pending/blocked masks.
- Delivery at safe user-return boundaries.
- Signal frames and `sigreturn`-like restoration.
- Default/ignore/handler actions.
- Process/thread targeting.
- Interrupted syscalls and restart policy.
- Job-control signals.
- Malformed signal-frame rejection.

## PHASE 27 — Process Lifecycle and Serviceable Init

- Reliable `fork`/spawn/exec/wait/exit semantics.
- Descriptor inheritance and close-on-exec.
- Orphan/zombie handling.
- Environment and argument inheritance.
- Minimal PID 1/init responsibilities.
- Service startup/shutdown ordering.
- Crash containment.
- Single-user boot target without an unnecessary enterprise init system.

## PHASE 28 — Low-Level Graphics and Display Hardware Preparation

- Framebuffer discovery and ownership.
- Pixel formats and stride handling.
- Scanout/display abstraction.
- EDID/monitor identification where practical.
- Basic mode information.
- GPU/PCI resource discovery.
- DMA/IOMMU boundaries for future graphics.
- Display reset/recovery diagnostics.
- Text-console coexistence.

**Important:** this is hardware preparation only. It is not GUI work.

## PHASE 29 — GPU Driver Foundation

- Generic GPU device model.
- Command submission abstraction.
- GPU memory objects.
- Fence/synchronization model.
- Interrupt/error reporting.
- Reset and hang recovery architecture.
- VRAM/system-memory ownership.
- Security boundaries for GPU command buffers.

**No desktop environment is permitted here.**

## PHASE 30 — AMD GPU / RX 6000-Class Hardware Preparation

- AMD PCI identification and BAR handling.
- Required MMIO discovery.
- Firmware-loading architecture if needed.
- Display-engine groundwork.
- GPU queue abstraction.
- Reset/recovery diagnostics.
- Physical evidence for the chosen target GPU.

**Gate:** supported AMD GPU hardware is understood enough for later display/GUI work without faking acceleration.

## PHASE 31 — USB Storage and Removable Media

- USB mass-storage class architecture.
- Bulk-only transport where applicable.
- Removable block-device lifecycle.
- Media insertion/removal races.
- Partition/media identification.
- Safe read-only handling of unknown media.
- Write-protect and flush semantics.
- Recovery after cable/device removal.

## PHASE 32 — Partitioning and Disk Discovery

- GPT parser with CRC validation.
- Protective MBR handling.
- Partition type/attribute interpretation.
- Sector-size validation.
- Device naming and stable identity.
- Corrupt/ambiguous table behavior.
- Read-only inspection mode.
- Destructive partition operations require explicit confirmation.

## PHASE 33 — Filesystem Recovery and Power-Loss Semantics

- RixFS journal/replay correctness.
- Metadata checksum validation.
- Orphan recovery.
- Interrupted write handling.
- Dirty mount detection.
- fsck repair safety.
- Read-only emergency recovery.
- Simulated and real power-loss testing on disposable media.

**Gate:** a failed write or unclean shutdown does not silently corrupt the filesystem.

## PHASE 34 — Bootable Root Filesystem and System Layout

- Stable `/`, `/bin`, `/sbin`, `/lib`, `/etc`, `/dev`, `/tmp`, `/home`, `/var`, `/usr` policy.
- Single-user home directory lifecycle.
- Read-only vs writable system areas where useful.
- Runtime-state directory policy.
- Device-node strategy.
- Logs and crash dumps.
- System configuration format.
- Recovery shell layout.

## PHASE 35 — First Usable RixuriOS Milestone

- Physical desktop boots into the real root filesystem.
- Primary user reaches a real shell.
- Real dynamically linked utilities execute.
- Files can be created, read, written, renamed and deleted.
- Processes can be started, stopped and waited for.
- Keyboard/input path works without test injection.
- Network can be configured on at least one supported physical NIC.
- Reboot/poweroff works.
- Recovery shell exists.
- Known limitations are visible to the user.

**Milestone:** **usable terminal OS**, not yet release quality and not yet GUI.

---

# SYSTEM MATURITY — PHASES 36–59

## PHASE 36 — Device Discovery and `/dev` Strategy

- Stable device naming.
- Character/block device registration.
- Dynamic device-node creation.
- Device permissions for the single owner.
- Hotplug event path.
- Device removal cleanup.
- Hardware inventory tool.

## PHASE 37 — Proc-like and Sysfs-like Diagnostics

- `/proc`-style process/CPU/memory views.
- Hardware/device inventory interface.
- PCI/device diagnostics.
- Memory statistics.
- Scheduler statistics.
- Mount and filesystem state.
- Network interface statistics.
- Explicitly diagnostic, not a Linux ABI clone.

## PHASE 38 — System Call Completeness and ABI Freeze Candidate

- Inventory every syscall.
- Remove accidental ENOSYS gaps that are required by userspace.
- Verify argument widths and structure packing.
- Verify 32/64-bit assumptions within x86_64 ABI.
- Version compatibility metadata.
- Negative ABI tests.
- User/kernel structure fuzzing.

**Gate:** core ABI is stable enough for libc and system tools.

## PHASE 39 — Error Handling and Fault Containment

- Uniform kernel error taxonomy.
- Device error propagation.
- Process-fault containment.
- Panic policy for unrecoverable kernel faults.
- Userspace crash reporting.
- Last-error diagnostics without unsafe global state.
- Timeout visibility.
- Recovery-state reporting.

## PHASE 40 — Logging, Tracing and Crash Diagnostics

- Structured kernel log records.
- Log levels and filtering.
- Per-subsystem tags.
- Ring-buffer persistence where practical.
- Crash dump metadata.
- Boot-to-crash timeline.
- Syscall tracing.
- Scheduler/device/storage/network trace points.
- User-accessible diagnostic collection.

## PHASE 41 — Configuration System

- Stable text/config format.
- Boot configuration.
- Network configuration.
- Mount configuration.
- User preferences that are useful before GUI.
- Validation and atomic replacement.
- Safe defaults.
- Recovery from malformed configuration.

## PHASE 42 — Minimal Service Manager

- Service definition format.
- Dependency ordering.
- Start/stop/restart.
- Crash restart policy.
- Logging capture.
- Timeouts.
- Shutdown ordering.
- Single-user simplicity: no cluster/service-discovery requirements.

## PHASE 43 — System Initialization and Boot Targets

- Firmware → bootloader → kernel → init → services → login/shell.
- Boot phases and timing.
- Failure isolation.
- Safe mode/recovery target.
- Single-user normal target.
- Diagnostic boot target.
- Clean shutdown target.

## PHASE 44 — Login and Local Session Management

- One primary local user.
- Authentication boundary.
- Password/hash storage policy if passwords are used.
- Session creation.
- Environment setup.
- Home-directory preparation.
- TTY ownership.
- Automatic-login option only if explicitly chosen by the owner and clearly documented.

## PHASE 45 — Permissions, File Security and Privilege Hardening

- Correct mode-bit enforcement.
- Root-only operations.
- Setuid/setgid decision and security review.
- Capability-like primitives only if actually needed.
- Symlink race resistance.
- TOCTOU review.
- Secure temporary-file creation.
- Device-node privilege policy.

## PHASE 46 — Memory Hardening

- SMEP enforcement where supported.
- SMAP enforcement where supported.
- NX/W^X verification.
- Kernel stack protection.
- User stack guard pages.
- Heap poisoning/debug modes.
- Use-after-free diagnostics.
- Double-free detection.
- Page-table permission audits.

## PHASE 47 — DMA and IOMMU Security

- DMA mapping lifetime.
- Device isolation policy.
- IOMMU domains where available.
- Interrupt remapping where applicable.
- Invalid DMA fault diagnostics.
- Device reset cleanup.
- Driver-owned buffer lifetime audit.

## PHASE 48 — Network Robustness and External Connectivity

- TCP retransmission stress.
- Window/flow-control correctness.
- Fragmentation/MTU behavior.
- DNS timeout/retry/cache behavior.
- DHCP renewal.
- Link down/up recovery.
- NIC reset.
- Long-running connection tests.
- External-network evidence distinct from loopback/QEMU.

## PHASE 49 — Network Utilities and User Networking

- Interface listing/configuration.
- Route inspection.
- DNS configuration.
- Ping/ICMP utility.
- UDP/TCP diagnostic clients.
- Download/upload primitive.
- Local hostname configuration.
- Network failure explanations understandable to the owner.

## PHASE 50 — Time Synchronization

- RTC-to-system-clock initialization.
- Monotonic/realtime separation.
- Network time synchronization architecture.
- Clock adjustment policy.
- Slew/step safety.
- Offline behavior.
- Timestamp consistency in logs/files.

## PHASE 51 — Storage Performance and Reliability

- I/O queue tuning.
- Cache hit/miss metrics.
- Writeback throttling.
- Flush/FUA measurement.
- NVMe latency tracking.
- Recovery-time measurement.
- Large-file stress.
- Fragmentation tests.

## PHASE 52 — Process and Scheduler Performance

- Context-switch cost.
- Scheduler latency.
- Wakeup latency.
- CPU utilization accounting.
- SMP scaling measurements.
- Priority inversion detection.
- Starvation tests.
- Idle-power behavior where relevant to desktop PCs.

## PHASE 53 — Memory Pressure and OOM Behavior

- Global memory accounting.
- Per-process memory accounting.
- Allocation failure policy.
- OOM diagnostics.
- Safe process termination policy.
- Reclaim before failure.
- Mapping exhaustion tests.
- No silent memory corruption under pressure.

## PHASE 54 — Resource Limits for a Single-User Desktop

- Open-FD limits.
- Process/thread limits.
- Address-space limits.
- Memory limits.
- Pipe/IPC limits.
- Disk-space warning thresholds.
- Safe defaults rather than enterprise quota complexity.

## PHASE 55 — File Descriptor and Object Lifetime Audit

- FD namespace consistency.
- Close-on-exec.
- Duplication semantics.
- Reference-count correctness.
- Device/file/process object teardown.
- Concurrent close/read/write tests.
- Process death cleanup.

## PHASE 56 — Concurrency and Race Audit

- SMP race corpus.
- Lock-order validation.
- Interrupt/process interaction.
- Device removal during I/O.
- Filesystem concurrent operations.
- Signal/thread races.
- FD races.
- Stress runs with randomized scheduling where practical.

## PHASE 57 — Recovery Shell and Owner Recovery Toolkit

- Boot-to-recovery shell.
- Filesystem check/repair entry.
- Network diagnostics.
- Disk diagnostics.
- Kernel-log collection.
- Service disable/enable.
- Configuration rollback.
- Safe reboot/poweroff.
- Recovery documentation available offline.

**Single-user principle:** the owner must be able to repair the machine without another computer whenever reasonably possible.

## PHASE 58 — Update and Rollback Architecture

- Versioned system updates.
- Atomic update staging.
- Bootable previous version.
- Failed-update detection.
- Configuration preservation.
- User-data preservation.
- Rollback from recovery.
- Never overwrite the only known-good system blindly.

## PHASE 59 — System Integrity and Release Candidate Base

- Whole-system consistency checks.
- Boot/root/userspace compatibility verification.
- ABI freeze candidate.
- Hardware support matrix.
- Known-failure list reduced to explicit non-blockers.
- Recovery path verified.
- Release candidate build reproducibility.

**Milestone:** **serious daily terminal OS candidate**.

---

# RELEASE ENGINEERING — PHASES 60–79

## PHASE 60 — Supported Hardware Matrix

- Define exact supported motherboard/firmware classes.
- CPU feature requirements.
- NVMe devices tested.
- NICs tested.
- USB controllers tested.
- HID devices tested.
- GPU/display targets tested.
- Unsupported hardware is reported honestly.

## PHASE 61 — Physical Hardware Qualification

- Cold boot.
- Warm reboot.
- Poweroff.
- Repeated boot cycles.
- Long idle.
- CPU stress.
- Memory stress.
- Disk stress.
- Network stress.
- USB hotplug.
- Device reset/recovery.

## PHASE 62 — QEMU Regression Matrix

- BIOS/UEFI where supported by project policy.
- Single/multi-CPU.
- Low/high memory.
- NVMe variants.
- E1000 networking.
- USB/xHCI variants.
- Fault injection.
- Malformed disk/media cases.
- Boot failure cases.

## PHASE 63 — Boot Regression Suite

- Every known boot failure.
- UEFI memory-map variations.
- EBS retry.
- Page-table failures.
- Invalid ELF kernel.
- CPU feature absence.
- AP startup failure.
- Driver initialization failure.
- Recovery boot.

## PHASE 64 — Kernel Test Harness

- Allocator tests.
- Page-table tests.
- IPC tests.
- Scheduler tests.
- Syscall tests.
- Signal tests.
- Filesystem tests.
- Device-model tests.
- Networking tests.

## PHASE 65 — Userspace Test Harness

- libc conformance subset.
- ELF loader tests.
- TLS tests.
- pthread/futex tests.
- Shell parser tests.
- Utility exit-status tests.
- Path/permission tests.
- Network API tests.

## PHASE 66 — Fuzzing and Parser Security

- ELF fuzzing.
- Filesystem metadata fuzzing.
- HID report fuzzing.
- USB descriptor fuzzing.
- PCI capability fuzzing.
- Network packet fuzzing.
- Shell/parser fuzzing.
- Syscall argument fuzzing.
- Configuration fuzzing.

## PHASE 67 — Fault Injection and Recovery Testing

- I/O timeout injection.
- NVMe reset injection.
- USB disconnect during transfer.
- NIC link loss.
- Allocation failure.
- Page fault at every user boundary.
- Service crash.
- Corrupt configuration.
- Interrupted update.

## PHASE 68 — Power-Loss and Crash Consistency Lab

- Forced reset during filesystem writes.
- Forced reset during metadata updates.
- Interrupted update.
- Journal replay.
- Recovery boot.
- Repeated crash cycles.
- Data-integrity verification.

## PHASE 69 — Security Audit

- Kernel/user boundary.
- Uaccess TOCTOU.
- Privilege checks.
- FD namespace.
- DMA/IOMMU.
- Filesystem path traversal.
- Symlink races.
- ELF loader.
- TLS/thread state.
- Network parser.
- USB/HID parser.

## PHASE 70 — Performance Baseline

- Boot time.
- Shell startup.
- Process creation.
- Context switch.
- Syscall latency.
- Page-fault cost.
- File throughput.
- NVMe latency/throughput.
- Network throughput/latency.
- Memory overhead.

## PHASE 71 — Performance Regression Tracking

- Stable benchmark workloads.
- Compare builds by commit.
- Detect latency regressions.
- Detect memory regressions.
- Detect I/O regressions.
- Detect boot regressions.
- Keep correctness gates independent of performance scores.

## PHASE 72 — Resource Leak and Long-Run Testing

- 1-hour runs.
- 12-hour runs.
- 24-hour runs where practical.
- FD leak detection.
- Memory leak detection.
- Thread/process leak detection.
- Device object leak detection.
- Mount/unmount loops.
- Network reconnect loops.

## PHASE 73 — Hardware Hotplug and Recovery Matrix

- USB insertion/removal.
- HID insertion/removal.
- NIC link changes.
- Storage removal where supported.
- PCI hotplug only where target hardware supports it.
- Interrupt teardown.
- DMA teardown.
- User-visible recovery state.

## PHASE 74 — Bootloader and Recovery UX (Terminal Only)

- Clear boot menu.
- Normal boot.
- Recovery boot.
- Previous-system boot.
- Diagnostic boot.
- Kernel argument management.
- Safe defaults.
- No graphical boot dependency.

## PHASE 75 — Installer and Initial Disk Setup

- UEFI installation path.
- Disk selection with destructive-operation confirmation.
- GPT creation.
- RixFS creation.
- System installation.
- Boot entry creation.
- Initial user setup.
- Recovery partition/area where appropriate.
- Installation logs.

## PHASE 76 — Installer Safety and Abort Recovery

- Preview destructive changes.
- Require explicit confirmation.
- Detect wrong disk selection.
- Power-loss recovery during install.
- Interrupted install rollback.
- Corrupt target handling.
- Never silently format unknown media.

## PHASE 77 — Package/Software Distribution Foundation

- Native package format if justified.
- Package metadata.
- Dependency model.
- File ownership tracking.
- Install/remove/upgrade.
- Signature/integrity architecture.
- Transaction/rollback semantics.
- Offline installation from local media.

## PHASE 78 — Package Repository and Update Client

- Repository metadata.
- Download verification.
- Package signatures.
- Version selection.
- Dependency resolution.
- Atomic staging.
- Failed-download recovery.
- Update rollback integration.

## PHASE 79 — Reproducible Release Candidate

- Clean build from pinned toolchain.
- Rebuild verification.
- Installer image generation.
- Package repository snapshot.
- Hardware evidence bundle.
- Test report.
- Known-failure report.
- Recovery verification.

---

# PRE-GUI PRODUCT MATURITY — PHASES 80–99

## PHASE 80 — Native Developer Toolchain

- Native assembler/compiler support as practical.
- Debug symbols and stack traces.
- Static analysis tools.
- Object inspection tools.
- ELF inspection utility.
- System-call tracing utility.
- Kernel log analysis tools.

## PHASE 81 — Native Build Environment

- Make/build tool support.
- Shell scripting sufficient for system builds.
- Header/library installation.
- `/usr/include` policy.
- `/usr/lib` policy.
- Dynamic linker integration.
- Development package format.

## PHASE 82 — POSIX Compatibility Expansion

- Expand only APIs useful to real RixuriOS software.
- Directory/process/time APIs.
- Terminal APIs.
- Socket APIs.
- Signals.
- pthreads.
- mmap.
- Poll/select-style interfaces.
- Document unsupported behavior rather than pretending Linux compatibility.

## PHASE 83 — musl Integration

- Port/build musl against the RixuriOS syscall ABI.
- Thread/TLS support.
- Dynamic linker integration.
- Startup objects.
- libc ABI validation.
- `errno`, signals, pthreads and filesystem integration.
- Real dynamically linked standard C programs.

**Gate:** this is a genuine musl-based userspace, not merely a musl-shaped sysroot.

## PHASE 84 — Native Shell and Scripting Maturity

- Reliable shell parser.
- Functions and scripts.
- Robust quoting.
- Exit-status propagation.
- Signals/job control.
- Environment manipulation.
- Useful scripting primitives.
- Error messages suitable for actual administration.

## PHASE 85 — System Administration Toolkit

- `rix` native administration command.
- `rix status`.
- `rix diagnostics`.
- `rix hardware`.
- `rix network`.
- `rix storage`.
- `rix service`.
- `rix update`.
- `rix recovery`.
- All commands use documented public APIs.

## PHASE 86 — Documentation and Offline Manual

- System architecture manual.
- Syscall ABI reference.
- Driver model reference.
- Filesystem format reference.
- Recovery manual.
- Installation manual.
- Hardware support matrix.
- Troubleshooting manual.
- Offline `man`-like documentation.

## PHASE 87 — Observability for the Single Owner

- One-command diagnostic bundle.
- Boot timeline.
- Hardware inventory.
- Driver status.
- Filesystem status.
- Network status.
- Memory/CPU status.
- Recent crash information.
- Privacy-aware log collection.

## PHASE 88 — Backup and Restore

- User-home backup.
- Configuration backup.
- System-state metadata.
- Restore to fresh RixuriOS installation.
- Backup verification.
- Interrupted backup recovery.
- Local removable-disk backup.
- Optional network backup only if useful.

## PHASE 89 — Data Integrity and User Safety

- Checksums for critical stored metadata.
- Safe temporary files.
- Atomic configuration writes.
- Disk-full behavior.
- Permission failures.
- Read-only filesystem behavior.
- Unexpected device removal.
- Clear warnings before destructive actions.

## PHASE 90 — Desktop Hardware Compatibility Expansion

- More AMD/Intel desktop CPUs.
- More NVMe controllers.
- More Realtek/Intel NICs where worthwhile.
- More USB controllers.
- Common USB keyboards/mice.
- Selected AMD GPU targets.
- Motherboard firmware variation testing.
- Unsupported hardware diagnostics.

## PHASE 91 — Release Security Hardening

- Full SMEP/SMAP policy.
- W^X audit.
- ASLR validation.
- Stack guard validation.
- Heap hardening.
- Kernel pointer exposure review.
- Usercopy validation review.
- DMA/IOMMU audit.
- ELF loader security review.

## PHASE 92 — Release Reliability Soak

- Multi-day boot/use cycles.
- Repeated process creation.
- Repeated filesystem operations.
- Long network sessions.
- Long NVMe workloads.
- USB hotplug loops.
- Memory pressure.
- Recovery exercises.

## PHASE 93 — Release Regression Freeze

- No new feature without release-owner justification.
- Historical failures must remain covered.
- ABI freeze.
- Filesystem format freeze for release generation.
- Driver behavior freeze for supported hardware.
- Regression suite mandatory on every release candidate.

## PHASE 94 — Final Physical Hardware Acceptance

- Cold boot PASS.
- Warm reboot PASS.
- Poweroff PASS.
- Storage PASS.
- Filesystem PASS.
- USB/HID PASS.
- Network PASS.
- SMP/preemption PASS.
- Memory pressure PASS.
- Recovery PASS.
- Evidence archived per target machine.

## PHASE 95 — Release Candidate 1

- Build reproducibility PASS.
- Installer PASS.
- Boot PASS.
- Terminal PASS.
- Userspace PASS.
- Network PASS.
- Storage PASS.
- Recovery PASS.
- Security PASS.
- Documentation PASS.

## PHASE 96 — Release Candidate 2 / Bug-Fix Only

- Fix only release blockers and high-impact regressions.
- Re-run physical hardware matrix.
- Re-run power-loss tests.
- Re-run upgrade/rollback.
- Re-run full boot and userspace suites.

## PHASE 97 — Release Candidate 3 / Final Stability

- No known critical kernel crash.
- No known filesystem corruption path under supported workloads.
- No known unrecoverable update path.
- No unresolved supported-hardware blocker.
- Final installer image.

## PHASE 98 — RixuriOS 1.0 Release Preparation

- Versioning.
- Release notes.
- Hardware compatibility documentation.
- Installation media.
- Recovery media/path.
- Checksums/signatures.
- Reproducible build record.
- Known limitations.

## PHASE 99 — 1.0 Gate / Pre-GUI Baseline

This is the final gate before the graphical product.

Required:

- Stable boot.
- Stable kernel memory management.
- SMP and preemption.
- Real user address spaces.
- Dynamic ELF and TLS.
- Threads/futex/signals.
- Storage/filesystem recovery.
- USB/HID.
- Network/DNS/TCP recovery.
- libc/musl userspace.
- Shell/utilities.
- Installer/update/rollback.
- Recovery shell.
- Security hardening.
- Physical hardware evidence.
- Regression and soak tests.

**Gate:** RixuriOS is already a complete, usable **terminal-first single-user OS before GUI development begins.**

---

# FINAL PRODUCT — PHASE 100 ONLY

## PHASE 100 — Graphical Desktop / GUI

**This is the first and only phase whose completion means the RixuriOS graphical desktop product is complete.**

### 100.1 Display server / compositor foundation

- Display ownership.
- Output enumeration.
- Framebuffer/scanout.
- Rendering synchronization.
- Cursor handling.
- Multi-monitor architecture if the supported hardware warrants it.
- Recovery when a display/GPU component crashes.

### 100.2 Window system

- Windows/surfaces.
- Input routing.
- Focus.
- Keyboard/mouse integration.
- Clipboard.
- Basic drag/drop where useful.
- Window lifecycle.

### 100.3 Desktop shell

- Desktop/session startup.
- Application launcher.
- Task/window management.
- System status.
- Notifications.
- Terminal application.
- File manager.
- Basic settings.

### 100.4 Single-user UX

- Fast local login or configured automatic login.
- Owner session recovery.
- Clear crash/restart behavior.
- No unnecessary multi-user display-manager complexity.
- Terminal remains available if GUI fails.
- Recovery mode remains usable without GUI.

### 100.5 GUI security

- Application/window isolation policy.
- Clipboard boundaries.
- Input routing restrictions.
- Privileged-operation confirmation through safe system interfaces.
- GPU command-buffer security.
- Display-server crash containment.

### 100.6 GUI performance

- Frame pacing.
- Input latency.
- CPU/GPU utilization.
- Memory use.
- Compositor recovery.
- Benchmarking against the supported hardware matrix.

### 100.7 Final desktop acceptance

- Physical boot into graphical session.
- Keyboard and mouse.
- Terminal application.
- File management.
- Network configuration/use.
- System settings.
- Reboot/poweroff.
- GUI crash recovery to terminal/restart.
- Update/reboot/recovery cycle.
- No GUI-only path may make the system unrecoverable.

**FINAL PRODUCT GATE:**

> **RixuriOS 1.x is complete when Phase 100 passes and every pre-GUI phase required by the supported hardware/product profile has PASS evidence.**

---

# 4. Definition of Done

A phase is `COMPLETE` only when all applicable items below are satisfied:

- Specification exists.
- ABI/data model is documented.
- Real implementation exists.
- Build passes from a clean tree.
- Unit tests pass where applicable.
- Negative/boundary tests pass where applicable.
- QEMU evidence exists where applicable.
- Physical-hardware evidence exists where applicable.
- Historical regressions are covered.
- Security review is complete for the phase risk.
- Performance is measured where relevant.
- Failure and recovery behavior is documented.
- Known limitations are explicit.
- Documentation is updated.
- Checkpoint evidence is archived.

A phase may be `PARTIAL`, `DEGRADED`, `BLOCKED`, `NOT TESTED` or `UNSUPPORTED`, but those states must never be reported as complete.

---

# 5. RixuriOS maturity milestones

| Milestone | Target | Meaning |
|---|---:|---|
| Kernel foundation | 00–09 | Real kernel/process/VM/ABI foundation |
| Hardware foundation | 10–21 | Storage, USB, terminal and networking foundations |
| Userspace foundation | 22–27 | libc, dynamic ELF, mmap, threads, signals |
| First usable OS | **35** | Real physical terminal OS |
| Serious daily OS | **59** | Reliable terminal-first desktop OS |
| Release engineering | **79** | Installer, updates, recovery and release evidence |
| Pre-GUI 1.0 | **99** | Complete non-graphical OS ready for desktop layer |
| Graphical RixuriOS | **100** | Full desktop product |

**Important:** phase numbers are engineering milestones, not percentages of total OS completion. A project can have many early phases complete while still being far from a safe daily-use release.

---

# 6. Single-user optimization principle

Whenever a design choice is ambiguous, prefer the smallest architecture that is:

1. correct,
2. secure,
3. recoverable,
4. observable,
5. maintainable by one developer/owner,
6. extensible without forcing premature complexity.

Do not add enterprise abstractions simply because another operating system has them. Keep kernel primitives general enough for correctness, but keep product UX and service architecture intentionally focused on **one owner, one desktop, one local machine**.

The roadmap therefore prioritizes:

**memory correctness → SMP/preemption → user VM → ELF/dynamic linking/TLS → threads/signals → storage/filesystem recovery → USB/HID → network reliability → libc/musl → installer/update/rollback → physical hardware qualification → security/reliability → GUI.**

The GUI remains last because a graphical desktop is only valuable once the operating system underneath it is already a dependable operating system.