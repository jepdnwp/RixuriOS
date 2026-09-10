# RixuriOS Master Development Roadmap — v6

**Architecture:** x86_64 / AMD64 only  
**Kernel:** freestanding C11/C17 + minimal x86_64 assembly  
**Userspace:** Unix-like, musl/POSIX-oriented, dynamically linked  
**Product:** single-user desktop PC operating system  
**Operating target:** real AMD/Intel x86_64 desktop PCs first; QEMU is a development and regression platform, not a substitute for physical hardware  
**Product principle:** terminal-first, hardware-real, recovery-first  
**GUI:** **PHASE 100 ONLY — absolutely last**

> This is the single authoritative roadmap. It intentionally absorbs the previous roadmap extensions and corrections. There are no separate roadmap documents. Every phase is an engineering gate, not a wishlist item.

## Product scope

RixuriOS is a single-user desktop PC OS. The primary goal is a technically coherent, reliable and recoverable x86_64 PC operating system rather than a clone of Linux or Windows.

Laptop-only requirements are not product requirements: no battery UI, lid UX, laptop-specific power profiles or laptop-specific fan controls. ACPI is used where normal desktop-PC operation needs it: firmware discovery, MADT/interrupt routing, MCFG/PCIe discovery, timers, reboot/poweroff, thermal safety and device initialization. Suspend/resume is optional and only justified by an actual supported desktop target.

GUI work is deliberately postponed until Phase 100. Earlier graphics phases may prepare low-level framebuffer/GPU/display interfaces, but **no desktop GUI, graphical shell, graphical settings application, display manager or GUI productization may be treated as complete before Phase 100.**

## Non-negotiable rules

1. x86_64 only.
2. Kernel is freestanding and never links against glibc, musl or POSIX.
3. Userspace uses a documented RixuriOS syscall ABI.
4. Ownership, lifetime, locking, context and error semantics are explicit.
5. Device detection is never counted as driver completion.
6. QEMU and physical hardware are separate evidence classes.
7. No fake packets, fake disk data, synthetic hardware PASS records or test-only success paths.
8. `PASS` requires reproducible evidence.
9. Unknown/corrupt media fails safely and is never auto-formatted.
10. Destructive operations require explicit confirmation and disposable test targets.
11. Positive, negative, boundary, timeout and recovery tests are required according to subsystem risk.
12. Security is reviewed at privilege, parser, DMA, filesystem, IPC and device boundaries.
13. Every historical failure becomes a regression test.
14. Performance cannot hide correctness defects.
15. Documentation, diagnostics and reproducibility are implementation work.
16. `SKIP`, `ENOSYS`, `NOT TESTED`, `DEGRADED`, `BLOCKED`, `FAIL` and `UNSUPPORTED` are explicit states; none means COMPLETE.
17. No phase may silently replace a real implementation with a demo implementation.
18. ABI changes require an impact review and compatibility record.
19. Hardware support is per-device/per-platform and evidence-backed.
20. GUI cannot consume capacity while core OS gates remain open.

## Universal workflow

`SPEC → ABI/DATA MODEL → DESIGN → IMPLEMENT → BUILD → UNIT → NEGATIVE → QEMU → INTEGRATION → HARDWARE → REGRESSION → SECURITY → PERFORMANCE → DOCUMENT → CHECKPOINT`

Every phase must record changed files, ABI changes, ownership rules, locking/context rules, error codes, recovery behavior, hardware assumptions, test commands, observations, evidence locations and unresolved limitations.

## Checkpoint vocabulary

`CP0 SPEC`, `CP1 BUILD`, `CP2 UNIT`, `CP3 BOOT`, `CP4 INTEGRATION`, `CP5 HARDWARE`, `CP6 REGRESSION`, `CP7 SECURITY`, `CP8 PERFORMANCE`, `CP9 DOCS`, `CP10 RELEASE`.

---

# PHASE 00 — Governance, Source of Truth and Reproducible Build

- Canonical source tree and generated-file policy.
- Host/cross-toolchain separation and version pinning.
- Deterministic compiler/linker flags and image generation.
- Debug/release profiles, symbols, maps and build provenance.
- Clean-host and offline build procedures.
- CI build, unit-test and artifact retention policy.
- ABI/versioning policy, architecture decision records and changelog.
- Checkpoint ledger, hardware inventory schema and known-failure register.
- Release-blocker classification and reproducible evidence format.

**Gate:** another developer can rebuild the same image from a clean environment and obtain traceable artifacts.

# PHASE 01 — UEFI Boot and Firmware Handoff

- Correct EFI table/function-pointer layouts and calling conventions.
- ELF64 validation with overflow, alignment and canonical-address checks.
- PT_LOAD allocation/copy/zero-fill and kernel entry contract.
- ACPI RSDP, GOP and firmware-memory-map discovery.
- Final memory-map capture and `ExitBootServices()` retry handling.
- Boot handoff structure versioning and firmware quirk reporting.
- Boot failure diagnostics that preserve the last valid firmware state.

**Evidence:** real UEFI boot, memory map, ACPI discovery, EBS success and historical UEFI exception regressions.

# PHASE 02 — CPU Bring-up, PMM, VMM and Kernel Memory

- CPUID/MSR wrappers and CPU feature policy.
- NX/WP/SMEP/SMAP capability detection and staged enforcement.
- Physical memory descriptor parsing, reserved ranges and frame ownership.
- DMA-capable allocations, alignment and reference accounting.
- 4/5-level paging policy, kernel/user mappings and permission transitions.
- Page-fault entry/diagnostics and safe user-fault termination.
- Real kernel heap allocation/free, coalescing/slabs, alignment and leak diagnostics.
- Guard/debug allocation modes and allocator corruption detection.

**Gate:** kernel memory can be allocated and reclaimed without a permanent monotonic heap.

# PHASE 03 — GDT, TSS, IDT, Exceptions and Interrupt Entry

- GDT kernel/user segments and TSS installation.
- IST stacks for critical exceptions.
- Stable trap-frame ABI and register preservation.
- Exceptions 0–31 with useful diagnostics.
- IRQ entry/return, nesting rules and EOI ownership.
- PIC compatibility/disable path and spurious IRQ handling.
- Interrupt-safe logging and fault-to-process policy.
- Negative tests for malformed return state and nested faults.

# PHASE 04 — ACPI, LAPIC, IOAPIC, Timers and Initial SMP

- RSDP/XSDT/RSDT checksum and table validation.
- MADT CPU/LAPIC/IOAPIC entries and interrupt-source overrides.
- IOAPIC polarity/trigger configuration and MSI/MSI-X groundwork.
- LAPIC/x2APIC policy, IPI routing and CPU startup.
- AP trampoline, per-CPU state and CPU online/offline states.
- APIC timer, HPET/PIT compatibility and monotonic time foundation.
- TLB shootdown protocol and cross-CPU rendezvous design.

**Gate:** at least two real CPU execution contexts operate without global-single-CPU assumptions.

# PHASE 05 — Kernel Synchronization, Wait Queues and Workqueues

- Spinlocks, IRQ-save locks, mutexes, RW locks and semaphores.
- Wait queues and sleep/wakeup ownership.
- Atomic reference counting and object lifetime rules.
- Lock-order documentation and deadlock diagnostics.
- Interrupt-safe vs sleepable-context annotations.
- Kernel worker threads and deferred interrupt work.
- Cancellation, shutdown and worker-drain semantics.
- Lock contention tracing and stress tests.

# PHASE 06 — Process, Thread and Preemptive Scheduler Core

- PID/TID allocation and reuse protection.
- Process objects, parent/child relationships and exit/zombie state.
- Kernel threads and user-thread CPU context structures.
- Kernel stacks, TLS/thread-pointer architecture and lifetime management.
- Timer-driven preemption and per-CPU run queues.
- Priority/fairness policy, sleep/wakeup and idle threads.
- SMP load balancing, affinity, starvation detection and scheduler accounting.
- Context-switch and scheduler-latency instrumentation.

**Gate:** a real userspace workload is preempted, sleeps, wakes and migrates safely across CPUs.

# PHASE 07 — Syscall ABI and User/Kernel Boundary

- Stable syscall numbering/versioning and compatibility policy.
- Syscall entry/return and kernel-stack transition.
- Canonical-address, range and access validation.
- Safe copy-in/copy-out and fault-safe uaccess.
- FD/object validation and reference acquisition.
- Error/errno mapping and restartable syscall policy.
- Syscall tracing and ABI conformance tests.
- Malformed pointer, oversized argument and race-oriented tests.

# PHASE 08 — User Address Spaces and ELF64 Execution

- Independent user page tables and kernel/user separation.
- ELF header/program-header validation.
- PT_LOAD mapping, BSS zeroing and alignment.
- User stack with guard page and correct ABI alignment.
- `argc/argv/envp/auxv` construction.
- PIE/non-PIE policy and future ASLR integration.
- `exec` address-space replacement and complete teardown.
- User page-fault isolation and invalid-executable rejection.

**Gate:** a genuine ring-3 process runs independently of the kernel address space.

# PHASE 09 — IPC, Pipes, Signals, Events and Shared Memory Foundation

- Anonymous pipes and named FIFOs.
- Event/wait objects and pollable IPC.
- Shared memory with explicit permissions and lifetime rules.
- Process groups and signal-state architecture.
- Signal delivery/return-frame design.
- Unix-domain socket architecture and descriptor passing.
- IPC object reference counting and cleanup on process death.
- Deadlock, cancellation and partial-read/write semantics.

# PHASE 10 — PCIe, MCFG, MMIO, DMA and Device Model

- PCI/PCIe configuration and ECAM/MCFG discovery.
- Capability-list parsing with loop/length validation.
- BAR sizing, MMIO mapping and resource ownership.
- Bus-master/DMA mapping API and cache-coherency rules.
- MSI/MSI-X allocation and interrupt ownership.
- Formal bus/device/driver objects.
- Probe/remove/reset lifecycle and dependency ordering.
- PCI bridge traversal, multifunction devices and hotplug groundwork.
- IOMMU abstraction so drivers do not directly own security policy.

# PHASE 11 — Storage Core and Block Layer

- Block-device registry and stable device identity.
- BIO/request objects, scatter-gather and queue depth.
- Read/write/flush/FUA/barrier semantics.
- Completion callbacks and cancellation ownership.
- Timeout, retry and device-reset policy.
- Page/buffer cache and dirty/writeback lifecycle.
- Direct-I/O coherence rules.
- Storage error classes and recovery states.

# PHASE 12 — Real NVMe Driver

- Controller reset/enable and CAP/VS/CC/CSTS validation.
- Admin queues and Identify Controller/Namespace.
- I/O submission/completion queues and phase tags.
- PRP/SGL DMA construction and alignment checks.
- Polling and interrupt completion paths.
- Real read/write/flush and namespace lifecycle.
- Timeout, abort and controller-reset recovery.
- Error-status translation into block-layer semantics.
- QEMU plus physical NVMe evidence ladder.

**Never count PCI detection, BAR mapping or Identify alone as NVMe completion.**

# PHASE 13 — VFS and RixFS Core

- Vnode/inode/dentry/superblock/file abstractions.
- Mount tree, path resolution and namespace ownership.
- Directory iteration, lookup, create, unlink, mkdir and rmdir.
- Open-file-description offsets and FD table semantics.
- Permissions, mode bits and credential checks.
- Symlink/hard-link design and safe path traversal.
- RixFS superblock, inode, extent/data, directory and free-space formats.
- Versioned on-disk specification and incompatibility handling.
- fsck architecture and read-only emergency mount.

# PHASE 14 — Time, RTC and Desktop-PC Platform Management

- Monotonic and realtime clocks with clear clocksource ownership.
- RTC/CMOS abstraction where available.
- Timer/sleep APIs and timeout monotonicity.
- ACPI functions needed for normal desktop PCs.
- Reboot, poweroff and reset fallback paths.
- CPU idle states where useful.
- Thermal safety hooks where firmware exposes them.
- Optional suspend/resume only after a concrete desktop target requires it.

**Explicitly excluded:** battery UX, lid UX and laptop-specific power profiles.

# PHASE 15 — USB/xHCI Core

- xHCI capability/operational/runtime register model.
- DCBAA, scratchpads, command and transfer rings.
- Event ring/ERST and TRB cycle ownership.
- Slot/device/input-context lifecycle.
- Port reset, Address Device and Configure Endpoint.
- Control/bulk/interrupt transfer engines.
- Interrupters/MSI/MSI-X and DMA ordering.
- Timeout, controller reset and device recovery.
- Hotplug/disconnect race handling.
- Historical Address Device completion-code regressions retained forever.

# PHASE 16 — USB HID, Keyboard, Mouse and Input

- Descriptor and HID report parsing with bounds checks.
- Boot/report protocol support.
- Interrupt-IN lifecycle and endpoint recovery.
- Keyboard modifiers, press/release/repeat and rollover.
- Mouse buttons, motion and wheel events.
- Timestamped input event ABI.
- Device disconnect cleanup and hotplug.
- xHCI → USB → HID → input → TTY integration.

**Synthetic key injection cannot close the physical HID gate.**

# PHASE 17 — TTY, PTY, Console and Terminal Engine

- TTY and PTY master/slave objects.
- Canonical/raw modes and termios-like controls.
- Input/output queues and flow control.
- UTF-8 and ANSI/VT parser.
- Terminal dimensions and resize events.
- Controlling terminal/session/process-group integration.
- Terminal parser fuzzing and malformed escape handling.
- Console recovery path when userspace terminal components fail.

# PHASE 18 — Shell, Job Control and Command Execution

- Parser, quoting, escaping, expansion and globbing.
- Pipelines and redirections.
- Environment construction and safe inheritance.
- Command lookup without privileged PATH assumptions.
- Foreground/background jobs and process groups.
- Signal-aware job control.
- Command substitution and built-in framework.
- Exit status and failure propagation.

**Gate:** the OS is genuinely usable from a terminal without private test interfaces.

# PHASE 19 — Unix Utilities and Base Userland

- Filesystem utilities: `cat`, `cp`, `mv`, `rm`, `mkdir`, `rmdir`, `ls`, `find`.
- Text tools: `grep`, `sort`, `head`, `tail`, `printf`, `echo`.
- Process tools: `ps`, `kill`, `env`, `pwd`.
- Storage tools: `mount`, `umount`, `df`, `du`.
- Diagnostics: kernel-log reader, hardware inventory and network inspection.
- Administrative tools use documented RixuriOS interfaces.
- Correct exit status, stderr and signal behavior.
- No Linux-private assumptions hidden in utilities.

# PHASE 20 — Users, Groups, Credentials, Sessions and Security Policy

- UID/GID and supplementary groups.
- Credential inheritance and replacement.
- File permission checks and ACL architecture where justified.
- Privileged operation policy and kernel enforcement.
- Login/session ownership and controlling-terminal policy.
- Credential lifetime/revocation.
- Audit events and security logs.
- ASLR, stack protection and W^X integration.
- Explicit root/administrator model without trusting a userspace wrapper.

# PHASE 21 — Networking Stack and Real Device Integration

- Ethernet framing and interface abstraction.
- ARP, IPv4, ICMP, UDP and TCP state machine.
- TCP ordering, ACKs, retransmission, windows and connection teardown.
- Socket API with blocking/nonblocking semantics.
- Routing-table architecture and interface configuration.
- DNS resolver architecture.
- E1000/QEMU reference driver.
- RTL8125 real RX/TX, DMA rings, interrupt path and reset/recovery.
- Physical networking evidence separated from loopback/guest evidence.

# PHASE 22 — libc, POSIX Compatibility and C Runtime Surface

- C headers, errno, strings, memory, stdio and formatted I/O.
- Allocation APIs and environment handling.
- Directory/time/process/filesystem wrappers.
- Socket and signal wrappers.
- pthread synchronization surface preparation.
- `getopt`, `sysconf`, `getpagesize` and selected utility APIs.
- Compatibility matrix with implemented/partial/unsupported/not-tested states.
- Sysroot/bootstrap work without falsely claiming a complete musl port.

**Gate:** static C/POSIX surface is honest and documented; dynamic linking/TLS/threading remain deferred to later gates.

# PHASE 23 — Dynamic ELF Loader, Shared Libraries and TLS

- PT_INTERP/PT_DYNAMIC validation.
- DT_NEEDED/SONAME dependency graph.
- REL/RELA relocations, symbol tables and hash tables.
- GOT/PLT and binding policy.
- PIE and secure library search-path policy.
- Constructors/destructors and dependency cycles.
- `dlopen`, `dlsym`, `dlclose`, `dlerror`.
- PT_TLS, static/dynamic TLS models and `%fs` thread pointer.
- Auxiliary-vector completeness.
- Malformed ELF/shared-object fuzz corpus.

**Gate:** a real dynamically linked program runs without a test-only loader.

# PHASE 23A — Native Rix Privileged Command Interface

- `rix status`, `diagnostics`, `shutdown`, `poweroff`, `reboot`, `halt`.
- Service, user, group, mount, network and storage subcommands.
- Parser → authorization → syscall ABI → privileged subsystem architecture.
- UID/GID/root policy enforced in the kernel.
- Fail-closed authorization and explicit audit records.
- Safe environment/FD inheritance.
- Symlink, traversal, TOCTOU and malformed-argument testing.
- Shutdown/reboot race testing and physical evidence where applicable.

# PHASE 24 — Kernel Threads, Futexes and POSIX Concurrency Runtime

- Kernel/user thread lifecycle and TID semantics.
- Futex wait/wake/requeue and timeout correctness.
- pthread mutex/cond/rwlock/semaphore mapping.
- Thread-local errno and TLS cleanup.
- Join/detach/cancellation semantics.
- Robust synchronization where justified.
- Scheduler interaction with blocked threads.
- Priority inversion diagnostics.
- Multithreaded fork/exec rules.

# PHASE 25 — Hardened Memory, Fault Recovery and Process Isolation

- Real heap reclaim, slabs and coalescing.
- Guard pages and kernel/user stack protection.
- SMEP/SMAP where supported.
- W^X and NX enforcement.
- Hardened usercopy and uaccess fault recovery.
- ASLR architecture.
- Copy-on-write and memory reference auditing.
- OOM detection, limits and controlled process termination.
- Use-after-free/double-free diagnostics.

# PHASE 26 — Advanced VFS, Filesystem Semantics and Recovery

- Complete `stat`/`fstat`/`lstat` metadata and timestamps.
- Symlinks, hard links and link-count correctness.
- Atomic rename and cross-directory semantics.
- `fsync`/`fdatasync` durability contracts.
- `dup`/`dup2`/`dup3`, `CLOEXEC` and FD inheritance.
- Advisory locking and directory-FD APIs.
- Path-cache invalidation and race-resistant lookup.
- Dirty/unmount/device-disappearance recovery.
- Power-loss regression corpus.

# PHASE 27 — Advanced Networking and System Services Foundation

- Complete TCP retransmission/window/congestion behavior.
- DNS resolver with timeout/fallback behavior.
- Scheduler-integrated blocking sockets.
- Network configuration persistence.
- Service manager architecture.
- Logging service and structured log transport.
- Time synchronization architecture.
- Local IPC/service sockets.
- Network resource limits and failure recovery.

# PHASE 28 — Graphics Hardware Preparation — NO GUI

This is **not GUI productization**. It only prepares low-level graphics interfaces so later hardware qualification does not force a redesign.

- Generic framebuffer and scanout buffer abstraction.
- EDID parsing and mode description.
- Display connector/hotplug model.
- GPU PCI discovery and resource ownership.
- VRAM/GTT abstraction where needed.
- Software-rendering reference path.
- GPU reset/fault state model.
- Fence/synchronization primitives.
- No desktop shell, graphical login or GUI applications are allowed to close this phase.

# PHASE 29 — Package, Toolchain and Developer Environment

- Reproducible cross compiler and sysroot.
- Assembler/linker/debugger integration.
- SDK headers and syscall ABI packages.
- Package format and metadata.
- Dependency solver and transaction model.
- Signed repository metadata.
- Debug/source package policy.
- Offline development environment.
- Application templates and developer documentation.

# PHASE 30 — Installer, Recovery Environment and System Lifecycle

- Real hardware discovery in installer/recovery.
- GPT/ESP validation and explicit disk selection.
- RixFS creation only after confirmation.
- Bootloader installation and repair.
- Initial user/account setup.
- Network configuration.
- Rescue shell and read-only recovery.
- Backup/restore and migration architecture.
- Interrupted-install recovery and rollback.

# PHASE 31 — Security Hardening and Audit

- Threat model for kernel, syscall, IPC, storage, network and drivers.
- SMEP/SMAP/NX/W^X and stack protection.
- ASLR and hardened memory layout.
- Syscall/parser/filesystem/network fuzzing.
- Privilege escalation regression corpus.
- TOCTOU and FD-lifetime auditing.
- DMA/IOMMU security review.
- Secret/key lifetime rules.
- Secure update/signature architecture.
- Security findings have explicit severity and release impact.

# PHASE 32 — Performance, Reliability and Soak Testing

- Boot, syscall, context-switch and scheduler latency baselines.
- Page-fault, allocator and IPC latency.
- NVMe/SATA throughput and tail latency.
- Ethernet throughput, loss and socket latency.
- Memory-pressure and OOM behavior.
- 24-hour QEMU stress and multi-day physical-PC soak.
- Repeated reboot, mount/unmount and controller-reset loops.
- Leak/deadlock/panic rate tracking.
- Regression thresholds tied to hardware/test configuration.

# PHASE 33 — Physical PC Hardware Qualification

- Maintain machine-readable motherboard/UEFI/CPU/RAM inventory.
- Record PCIe topology and exact device IDs.
- Qualify NVMe and SATA storage paths.
- Qualify RTL8125 and E1000 reference networking.
- Qualify xHCI and real USB HID.
- Record GPU discovery and low-level display evidence.
- Capture serial/framebuffer logs, exact commit and image hash.
- Maintain per-device PASS/FAIL/NOT TESTED status.
- Never convert a physical hang into a source-level PASS.

# PHASE 34 — Pre-Product Release Gate

- Mandatory kernel, memory, scheduler and ABI gates closed.
- Storage/filesystem corruption and recovery gates closed.
- Networking and physical driver gates reviewed.
- Security-critical findings resolved or explicitly release-blocked.
- Recovery environment proven on real hardware.
- Reproducible build and artifact hashes verified.
- Known-limitations and unsupported-hardware register complete.
- **No GUI product work may close here; this is a readiness gate only.**

# PHASE 35 — Production Service Manager and System Lifecycle

- PID 1/service manager.
- Dependency graph and startup ordering.
- Restart and crash-loop policy.
- Readiness/failure states.
- Privilege dropping and service resource limits.
- Structured service logs.
- Shutdown transaction graph.
- Recovery/single-user mode.
- Native `rix service` integration.
- Service state remains observable without a GUI.

# PHASE 36 — Production SMP, CPU Topology and NUMA

- Package/core/thread topology from CPUID/ACPI.
- Per-CPU data and allocators.
- CPU online/offline state machine.
- Affinity and isolation.
- Scheduler domains and load balancing.
- NUMA node discovery and locality policy.
- Batched TLB shootdowns and remote calls.
- Stop-the-world coordination and CPU-failure recovery.
- Lock contention and cross-CPU latency instrumentation.

# PHASE 37 — Advanced Virtual Memory and Memory Pressure

- Demand paging and anonymous/file-backed mappings.
- Complete `mmap`, `munmap`, `mprotect`, `brk` semantics.
- VMA interval management and guard regions.
- COW for fork and shared/private mapping rules.
- Page-cache integration.
- Reference accounting and reclaim.
- Working-set/OOM policy and per-process memory limits.
- Fault throttling and memory-pressure diagnostics.
- Deterministic OOM/COW/mapping regression tests.

# PHASE 38 — Complete Process Model and Job Control

- Sessions/process groups and controlling terminals.
- Orphaned groups and terminal ownership.
- Zombie/reaping correctness.
- Parent-death semantics.
- Complete wait-family behavior.
- Foreground/background control.
- Resource accounting and per-process limits.
- Safe exec transition and environment lifetime.
- Fork/exec storm stress without PID/FD/VMA leaks.

# PHASE 39 — Signals, Timers and Asynchronous Event Model

- Signal masks and pending queues.
- Process- and thread-directed signals.
- Signal frames and validated `sigreturn`.
- Alternate signal stacks.
- Interrupted syscall restart policy.
- Disposition inheritance/reset across exec.
- Synchronous CPU-fault signals.
- Process/realtime timers and cancellation races.
- Timer overrun accounting and clock correctness.

# PHASE 40 — Complete POSIX Thread Runtime

- pthread create/join/detach/cancel.
- TLS allocation and destruction.
- Thread-local errno.
- Futex contention and priority inversion handling.
- Mutex/cond/rwlock/semaphore correctness.
- Fork behavior for multithreaded processes.
- Signal masks per thread.
- Thread resource limits.
- Real multithreaded application qualification.

# PHASE 41 — Runtime Linker and Dynamic ELF Hardening

- Full relocation validation.
- Symbol lookup/hash-table performance and correctness.
- Library dependency graph and cycle handling.
- Secure RPATH/RUNPATH policy.
- PIE/ASLR integration.
- Lazy vs immediate binding policy.
- Shared-object lifetime and reference counts.
- Constructor/destructor ordering.
- Loader crash isolation and fuzz corpus.

# PHASE 42 — Real musl Integration and C/POSIX Runtime

- Port the actual selected musl version rather than maintaining an imitation forever.
- Complete syscall layer and architecture startup files.
- TLS/pthread integration.
- Dynamic loader integration.
- Signals, sockets, polling, clocks and filesystem calls.
- C library conformance and real application tests.
- Thread-safe errno and cancellation behavior.
- Document unsupported POSIX features instead of silently emulating them.

# PHASE 43 — VFS Completion and Namespace Semantics

- Complete stat/link/rename/open semantics.
- Symlink traversal and loop detection.
- Mount namespaces only where useful to the single-user product.
- Read-only/bind mount semantics.
- Dentry/inode lifetime correctness.
- FD inheritance and `CLOEXEC`.
- Directory-FD and relative-path APIs.
- Path-resolution race tests and concurrent filesystem operations.

# PHASE 44 — RixFS v2 Journaling, Snapshots and Integrity

- Formal versioned on-disk format.
- Checksummed metadata and journal records.
- Ordered/writeback durability modes.
- Crash-consistency transaction model.
- Orphan cleanup and free-space verification.
- fsck repair classes and read-only emergency mount.
- Snapshot metadata and COW snapshot data.
- Sparse files/xattrs where product requirements justify them.
- Power-loss interruption during every critical metadata transition.

# PHASE 45 — Full IPv4/IPv6 and Socket Semantics

- IPv4 fragmentation/reassembly.
- IPv6 addressing/routing and ICMPv6.
- ARP and neighbor-discovery hardening.
- PMTU and MTU handling.
- TCP retransmission, congestion, windows and out-of-order queues.
- TIME_WAIT, keepalive and RST/FIN correctness.
- UDP edge cases and checksum validation.
- Blocking/nonblocking sockets and poll integration.
- Unix-domain sockets and descriptor passing.

# PHASE 46 — DNS, DHCP and Network Administration

- DNS stub resolver with UDP/TCP fallback.
- Resolver configuration and cache policy.
- DHCP client where required by supported desktop networks.
- IPv6 router-advertisement handling where adopted.
- Hostname/interface/routing configuration.
- Network statistics and socket inspection.
- Network diagnostics utilities.
- Service startup/shutdown integration.
- Packet loss, DNS timeout and link-loss recovery tests.

# PHASE 47 — Real Hardware Driver Qualification Framework

- Formal driver qualification records and test harness.
- PCIe/MSI/MSI-X/DMA evidence.
- NVMe, SATA/AHCI, RTL8125, E1000 and xHCI qualification.
- USB HID and removable-media qualification.
- GPU discovery and low-level display qualification.
- Per-driver firmware requirements and reset procedures.
- Timeout/error/hotplug behavior.
- Physical PASS/FAIL evidence tied to exact hardware and kernel commit.

# PHASE 48 — GPU and Display Hardware Foundation — NO GUI

- Display connector/mode model.
- EDID and mode validation.
- Framebuffer/scanout buffers.
- GPU memory ownership and mapping.
- Software-rendering reference path.
- GPU command/fence architecture where hardware support is mature.
- Reset/recovery and fault containment.
- Multi-monitor data model.
- **No window manager, desktop shell or graphical settings UI.**

# PHASE 49 — Audio Hardware and Media Foundations — NO GUI

- Audio device abstraction.
- Codec discovery and stream lifecycle.
- Playback/capture ring buffers.
- Sample format/rate handling.
- Underrun/overrun recovery.
- Device hotplug.
- Latency measurement.
- Userland audio service interface.
- No GUI audio mixer requirement before Phase 100.

# PHASE 50 — Desktop-PC Firmware, Thermal and Reset Platform

- ACPI namespace evaluation framework limited to product needs.
- Power/reset buttons and firmware reset methods.
- Thermal safety reporting.
- CPU idle policy.
- PCIe/device initialization dependencies.
- Optional desktop suspend/resume only after evidence-based target selection.
- Firmware failure containment and fallback reset paths.
- No battery, lid or laptop power-profile requirements.

# PHASE 51 — Security Architecture and Privilege Isolation

- W^X/NX, SMEP/SMAP and stack hardening.
- Kernel/user pointer validation.
- Fine-grained privileged operations.
- Capability model only where it materially improves the product.
- TOCTOU-resistant authorization.
- FD/object lifetime hardening.
- Driver attack-surface review.
- DMA threat model and IOMMU policy.
- Security regression suite for every privileged subsystem.

# PHASE 52 — Cryptography, Entropy and Trust Infrastructure

- Hardware/firmware entropy discovery.
- Early-boot entropy strategy.
- Kernel CSPRNG API.
- Reviewed cryptographic library boundary.
- Constant-time requirements for sensitive operations.
- Key lifetime and zeroization.
- Hash/integrity primitives.
- Secure-random userspace API.
- No custom cryptography where a reviewed implementation is appropriate.

# PHASE 53 — Secure Boot and Measured Boot

- UEFI Secure Boot compatibility.
- Signed boot artifact verification.
- Key rotation and revocation policy.
- Measured boot when TPM hardware is available.
- Boot-chain measurement records.
- Anti-rollback metadata.
- Signed kernel/module/package policy.
- Recovery-key path and offline verification tools.
- Explicit refusal/recovery on untrusted artifacts.

# PHASE 54 — System Lifecycle, Init and Service Reliability

- PID 1 state machine.
- Dependency ordering and cycle rejection.
- Restart/backoff/crash-loop policy.
- Readiness and health state.
- Service sandbox/resource limits.
- Privilege drop and environment policy.
- Structured logs and diagnostics.
- Shutdown transaction graph.
- Single-user recovery mode.

# PHASE 55 — procfs/sysfs/devfs and Introspection

- `/proc` process/CPU/memory/mount information.
- Safe open-file/process status reporting.
- `/sys` device hierarchy and driver binding state.
- `/dev` device nodes and major/minor allocation.
- Stable hardware identifiers.
- Hotplug/uevent-style notification if justified.
- Real device state only; no fabricated support status.
- Access-control policy for sensitive introspection.

# PHASE 56 — Observability, Crash Dumps, Tracing and Debugging

- Structured kernel logs with CPU/thread/process IDs.
- Rate limiting and persistent crash records.
- Panic reason, registers, stack and page-fault metadata.
- Loaded image/module list and recent IRQ/scheduler history.
- Storage/network/USB controller snapshots.
- Crash dump persistence and recovery extraction.
- Syscall/scheduler/lock/block/network tracing.
- Trace buffers with bounded memory usage.

# PHASE 57 — Testing, Fuzzing and Fault Injection Platform

- Kernel unit/component tests.
- Userspace ABI and libc conformance tests.
- ELF/path/filesystem fuzzing.
- USB descriptor/HID fuzzing.
- PCI capability/ACPI parser fuzzing.
- Network packet/socket fuzzing.
- Terminal escape-sequence fuzzing.
- Allocation/DMA/IRQ/device-timeout fault injection.
- Power-loss and controller-reset simulation.
- Test artifact retention and deterministic reproduction.

# PHASE 58 — Toolchain, SDK and Package Ecosystem

- Reproducible compiler/binutils or LLVM policy.
- Official sysroot and SDK.
- Debugger and profiling support.
- Package format and signed metadata.
- Dependency solver and transaction engine.
- Repository index and mirror model.
- Atomic install/remove/upgrade/rollback.
- Package ABI compatibility fields.
- Third-party developer workflow from clean host.

# PHASE 59 — Installer, Recovery and Disaster Recovery

- Safe disk discovery and selection.
- GPT/ESP creation and validation.
- RixFS creation only after explicit confirmation.
- Bootloader installation/repair.
- Initial user and network configuration.
- Standalone recovery environment.
- Offline filesystem check/repair.
- Boot configuration repair.
- Crash-log and hardware-inventory extraction.
- Backup/restore and interrupted-install recovery.

# PHASE 60 — Release Engineering and Compatibility Baseline

- CPU/motherboard/firmware matrix.
- NVMe/SATA/NIC/USB/GPU matrix.
- QEMU-version matrix.
- Reproducible release image and hashes.
- Clean-host rebuild.
- Complete changelog and known-issues database.
- ABI/filesystem compatibility review.
- Installer/recovery/update tests.
- Physical hardware qualification report.

# PHASE 61 — PC Platform Compatibility Matrix

- Define supported AMD/Intel desktop CPU generations.
- UEFI compatibility classes and firmware quirks.
- PCIe topology capture.
- NVMe/SATA/USB/NIC/GPU inventory.
- Unsupported-device reporting.
- Per-machine reproducible inventory artifact.
- Never expose a generic `supported=true` without evidence.

# PHASE 62 — SATA/AHCI Storage Driver

- AHCI discovery and HBA reset.
- Command list/FIS handling.
- DMA setup and cache rules.
- SATA Identify.
- Read/write/flush.
- NCQ architecture.
- Timeout, port reset and error recovery.
- Hotplug where the target requires it.
- QEMU and physical SATA qualification.

# PHASE 63 — Generic Block Devices and Storage Multiplexing

- Unified NVMe/AHCI/virtual block adapters.
- GPT/MBR/protective-MBR parsing.
- Stable partition/device naming.
- Queue scheduling and depth control.
- Device disappearance handling.
- Storage health/error reporting.
- Partition-to-filesystem dependency tracking.
- Block-layer regression suite across all backends.

# PHASE 64 — Partitioning, Formatting and Disk Administration

- GPT creation/editing with strict bounds validation.
- Partition alignment and resize rules.
- Filesystem creation tools.
- Dry-run and confirmation modes.
- Disk geometry/free-space inspection.
- Interrupted-operation recovery.
- Explicit target identity confirmation.
- Unknown/corrupt disks never auto-format.

# PHASE 65 — Complete File I/O Semantics

- `pread`/`pwrite` and vectored I/O.
- Append/truncate correctness.
- `fsync`/`fdatasync` semantics.
- `fcntl` and advisory locks.
- Directory-FD operations.
- `O_CLOEXEC`, `O_NONBLOCK` and related flags.
- Concurrent offset semantics.
- Accurate errno and short-I/O behavior.
- Storage error propagation from hardware to userspace.

# PHASE 66 — Unix Compatibility Surface Expansion

- `access`, `chdir`, `fchdir`, `getcwd`.
- chmod/chown policy.
- Link/unlink/rename behavior.
- Directory-stream runtime.
- Environment and system-information APIs.
- Resource-limit APIs.
- Compatibility tests against real programs.
- Explicit unsupported-interface documentation.

# PHASE 67 — Shell Completion and Advanced Interactive Userland

- History and persistent configuration.
- Line editing and cursor control.
- Tab completion from real filesystem/command state.
- Quoting/escaping correctness.
- Command substitution and pipelines.
- Job control and signal interaction.
- Built-in command framework.
- Completion/help system without GUI dependencies.

# PHASE 68 — Core Userland Utilities Expansion

- Complete filesystem/process/text utility families.
- `ps`, `kill`, `mount`, `umount`, `df`, `du`.
- Kernel-log and hardware diagnostics.
- Network administration utilities.
- Archive/checksum utilities as justified.
- Correct locale/time/terminal handling.
- Consistent exit-status/error conventions.
- Application compatibility tests for each promoted utility.

# PHASE 69 — Process Resource Limits and Accounting

- CPU-time accounting.
- Address-space limits.
- FD/process/thread limits.
- Kernel-object accounting.
- Per-user resource accounting.
- OOM diagnostics and runaway-process protection.
- Resource-usage syscall/API.
- Scheduler and memory accounting cross-checks.

# PHASE 70 — Device Model and Driver Lifecycle 2.0

- Formal bus/device/driver graph.
- Driver matching and deferred probing.
- Reference-counted device ownership.
- Probe/remove/reset callbacks.
- Hotplug events.
- Device dependency ordering.
- Fault recovery and re-probe.
- Driver diagnostics and lifecycle tracing.

# PHASE 71 — PCIe Advanced Features

- Hardened capability traversal.
- MSI/MSI-X completion.
- PCIe AER architecture.
- Link-status and negotiated-speed reporting.
- BAR conflict detection.
- Bridge/bus numbering.
- Multifunction devices.
- PCI reset mechanisms and device fault containment.

# PHASE 72 — IOMMU and DMA Isolation

- AMD IOMMU and Intel VT-d abstraction.
- Device/domain mappings.
- DMA aperture policy.
- Identity vs translated DMA policy.
- Invalidation and synchronization.
- Interrupt remapping where supported.
- DMA fault reporting.
- Driver isolation and bounce-buffer fallback.
- Malicious/corrupt DMA descriptor tests.

# PHASE 73 — USB Device Framework Beyond HID

- Hub support and enumeration tree.
- Device/config/interface/endpoint objects.
- Class-driver registration.
- Control/bulk/interrupt transfer framework.
- Isochronous architecture only where required.
- Disconnect/reconnect race handling.
- Device reset and recovery.
- USB descriptor fuzzing and malformed-device tests.

# PHASE 74 — USB Mass Storage and Removable Media

- BOT protocol.
- SCSI command layer.
- Inquiry/capacity/read/write.
- Sense-data interpretation.
- Removable-media detection.
- Safe unplug and filesystem remount behavior.
- Timeout/reset/recovery.
- Corrupted-media tests.
- Physical USB storage qualification.

# PHASE 75 — Networking Driver Completion

- RTL8125 complete RX/TX path.
- E1000 virtual and physical validation.
- Descriptor ownership and DMA mapping.
- Interrupt/MSI-X handling.
- Ring reset and link recovery.
- Link negotiation reporting.
- MTU configuration.
- Statistics and sustained-traffic stress.

# PHASE 76 — Network Security and Robustness

- Malformed IPv4/IPv6 packet handling.
- TCP state-machine hardening.
- Socket lifetime race testing.
- SYN/resource exhaustion policy.
- Ephemeral-port allocation.
- Optional firewall architecture if product needs justify it.
- Per-process socket ownership.
- Network syscall fuzzing.
- Packet-loss/reordering regression tests.

# PHASE 77 — IPv6 and Modern Network Features

- IPv6 address configuration.
- Neighbor discovery and router advertisements.
- Link-local addresses.
- Dual-stack sockets.
- IPv6 routing and ICMPv6.
- PMTU handling.
- IPv6 DNS behavior.
- Dual-stack application compatibility.
- Failure/recovery when IPv6 is unavailable.

# PHASE 78 — Timekeeping and Clock Correctness

- TSC synchronization across CPUs.
- Clocksource selection and fallback.
- Monotonic/realtime separation.
- Timer drift measurement.
- Sleep/wakeup accuracy.
- Timeout monotonicity.
- Filesystem timestamp correctness.
- 64-bit time representation and long-range correctness.
- Time syscall conformance.

# PHASE 79 — Kernel Debugger and Remote Debug Infrastructure

- Panic debugger entry.
- Register and memory inspection.
- Symbol lookup and stack unwinding.
- Breakpoint/watchpoint architecture.
- GDB-compatible remote protocol where practical.
- User/kernel address inspection.
- Thread/scheduler inspection.
- Deadlock investigation.
- Crash-to-debugger reproducibility.

# PHASE 80 — Kernel Sanitizers and Memory Debugging

- Allocation poisoning and redzones.
- Guard pages.
- Use-after-free/double-free detection.
- Slab consistency checks.
- Reference-count diagnostics.
- Lock misuse detection.
- Interrupt-context assertions.
- Usercopy instrumentation.
- Debug-only instrumentation separated from production policy.

# PHASE 81 — Concurrency Verification

- Lock dependency graph.
- Scheduler perturbation and randomized wake ordering.
- Race-oriented SMP stress.
- Interrupt timing injection.
- Futex contention tests.
- Concurrent filesystem operations.
- Concurrent network sockets.
- Deadlock watchdog.
- Reproducible seed recording for concurrency failures.

# PHASE 82 — Long-Running Reliability and Soak Program

- 24-hour QEMU stress.
- Multi-day physical-PC stress.
- Repeated reboot cycles.
- Repeated mount/unmount.
- Repeated device resets.
- Network and storage soak.
- Process churn and memory pressure.
- Crash/leak/deadlock rate tracking.
- Automatic collection of first-failure evidence.

# PHASE 83 — Application Compatibility Qualification

- Static C programs.
- Dynamically linked C programs.
- pthread applications.
- Terminal applications.
- Network clients.
- Filesystem-heavy programs.
- Build tools and text-processing workloads.
- Shell scripts.
- Representative third-party software.
- Per-application dependency, PASS/FAIL and regression records.

# PHASE 84 — Build Reproducibility 2.0

- Clean-room builds.
- Pinned compiler/tool versions.
- Deterministic source archives.
- Deterministic ELF/filesystem outputs where possible.
- Generated-file tracking.
- Build provenance and dependency/license inventory.
- Offline build mode.
- Release artifact hashes.

# PHASE 85 — Package Repository Infrastructure

- Package metadata and ABI compatibility schema.
- Dependency constraints and solver.
- Signatures and repository trust.
- Repository index and mirror support.
- Atomic install/remove/upgrade.
- Transaction rollback.
- Orphan cleanup.
- Package verification before execution.
- Reproducible package builds.

# PHASE 86 — System Recovery and Forensic Mode

- Boot-failure diagnosis.
- Safe/single-user shell.
- Read-only filesystem recovery.
- RixFS inspection and repair.
- Kernel crash-log extraction.
- Hardware inventory extraction.
- Network-disabled recovery option.
- Account recovery policy.
- Recovery-image integrity verification.
- Evidence-preserving diagnostics.

# PHASE 87 — Update, Rollback and Compatibility Policy

- Versioned kernel/userspace ABI policy.
- Package dependency migration.
- Atomic system update staging.
- Rollback-point creation.
- Interrupted-update recovery.
- Bootable previous version.
- Configuration/database migration rollback.
- Signed update verification.
- Explicit refusal of incompatible updates.

# PHASE 88 — Pre-GUI Desktop Infrastructure

This phase is **not the GUI**. It prepares the OS so that a future graphical layer can be added without weakening the terminal-first architecture.

- Stable login/session primitives without graphical UI.
- Input/display/audio device APIs usable by future clients.
- Application lifecycle service APIs.
- Clipboard/data-transfer protocol design only at ABI level.
- Desktop configuration storage schema.
- User-session environment and IPC contracts.
- Crash/restart semantics for future desktop services.
- No graphical shell or GUI product completion.

# PHASE 89 — Final Pre-GUI Compatibility and Hardware Gate

- Re-run complete kernel/user ABI matrix.
- Re-run filesystem corruption/recovery corpus.
- Re-run networking and driver qualification.
- Re-run SMP/preemption/TLS/pthread tests.
- Re-run physical PC matrix.
- Verify recovery from boot, storage, network and driver failures.
- Resolve critical security findings.
- Freeze pre-GUI public ABI contracts.
- Publish unsupported-hardware list.

**This is the final gate before any GUI product code is allowed to become a release dependency.**

# PHASE 90 — Release Candidate Without GUI

- Produce a terminal-first release candidate.
- Clean-host reproducible build.
- Installer and recovery qualification.
- Physical-PC boot/install/recovery matrix.
- Storage/network/USB qualification.
- Long-duration soak results.
- Application compatibility report.
- Security review and release-blocker list.
- Known limitations and rollback procedure.

**The OS must already be a usable, recoverable terminal-first PC operating system here.**

# PHASE 91 — Stable ABI and Compatibility Freeze

- Freeze syscall numbers and documented structures for the release family.
- Define ABI extension mechanism.
- Define feature discovery instead of version guessing.
- Compatibility tests for old binaries.
- Struct-size/version negotiation rules.
- Deprecation policy with measurable timelines.
- ABI break detection in CI.
- Documentation generated from authoritative definitions.

# PHASE 92 — Advanced Scheduler Quality

- Scheduler fairness measurement.
- Interactive terminal latency.
- CPU affinity policy.
- Load-balancing stability.
- Priority inversion diagnostics.
- Timer/preemption jitter measurement.
- Starvation detection.
- Scheduler regression corpus under SMP stress.

# PHASE 93 — Advanced Kernel Concurrency and RCU

- Evaluate RCU/read-mostly primitives where they simplify hot paths.
- Per-CPU data lifetime rules.
- Deferred reclamation.
- Lock contention reduction only after correctness evidence.
- Memory-ordering documentation.
- Architecture-specific barrier validation.
- Concurrency sanitizer integration.
- Regression tests for reclamation races.

# PHASE 94 — Advanced Memory Locality and Allocation

- NUMA-aware allocation where real hardware benefits from it.
- Per-CPU caches.
- Slab/arena tuning.
- Fragmentation measurement.
- Memory compaction only if required.
- Huge-page policy based on measurement.
- Cache-locality diagnostics.
- Memory overhead budgets.

# PHASE 95 — Storage Reliability and Health

- SMART/health interfaces where hardware exposes them.
- NVMe health/error-log reporting.
- SATA health reporting where available.
- Bad-block/error accounting.
- Storage wear/failure diagnostics.
- Read-only emergency transitions.
- Device timeout escalation policy.
- Health information exposed through real diagnostics APIs.

# PHASE 96 — Advanced Filesystem Administration

- Quotas only if product requirements justify them.
- Extended attributes and file metadata policy.
- Snapshot administration.
- Filesystem statistics.
- Defragmentation/maintenance tools only when useful.
- Offline fsck workflows.
- Backup/restore integration.
- Filesystem migration/version-upgrade tooling.

# PHASE 97 — Network Policy and Firewall Foundation

- Interface-level filtering architecture.
- Stateful policy model if required.
- Per-process socket policy where justified.
- Logging and rate limiting.
- Safe default policy.
- Rule validation and transaction rollback.
- Packet-filter fuzzing.
- No firewall feature is enabled by default without a tested policy model.

# PHASE 98 — Sandboxing and Application Isolation

- Define a minimal RixuriOS application-isolation model.
- Restrict filesystem access where useful.
- Restrict device access.
- Restrict network access where justified.
- Resource limits.
- IPC policy.
- Privilege dropping.
- Audit sandbox violations.
- Fail closed on malformed policy.

# PHASE 99 — Final Pre-GUI Product Audit

- Independent audit of kernel, memory, scheduler and ABI.
- Independent audit of storage/filesystem recovery.
- Independent audit of network/device paths.
- Security and privilege audit.
- Physical hardware evidence audit.
- Reproducible-build audit.
- Application compatibility audit.
- Recovery/rollback audit.
- No unresolved critical defect may be hidden by GUI plans.

**Only after Phase 99 passes may Phase 100 begin.**

# PHASE 100 — GUI: Final Graphical OS Productization

**This is the first and only phase whose purpose is the user-facing GUI. GUI is absolutely last.**

### Display and compositor
- Stable display backend over the previously qualified graphics interfaces.
- Window/surface model.
- Compositor and damage tracking.
- Hardware/software rendering fallback.
- Multi-monitor configuration.
- Cursor and display hotplug.

### Desktop/session
- Graphical login/session only if justified.
- Window management.
- Terminal emulator as a first-class native application.
- Application launcher.
- File manager.
- Settings/control center.
- Network administration UI.
- Storage administration UI with the same destructive-operation safeguards as CLI.
- System monitor and diagnostics UI.
- Notifications and clipboard.
- Keyboard/mouse configuration.
- Accessibility foundations.

### GUI security
- GUI clients remain ordinary processes.
- No GUI component bypasses syscall authorization.
- Privileged operations go through the same kernel policy and `rix` architecture.
- Untrusted window/input data is validated.
- Clipboard and inter-process data are permission-aware.
- GUI crash cannot compromise the terminal/recovery path.

### GUI reliability
- Compositor crash recovery.
- Application crash isolation.
- Display-driver reset handling.
- Input hotplug.
- Multi-monitor failure/recovery.
- Low-memory behavior.
- Session restart.
- Recovery to terminal without data corruption.

### GUI qualification
- QEMU reference display.
- Physical GPU/display qualification.
- Real keyboard/mouse input.
- Multi-monitor tests where supported.
- Long-running graphical soak.
- Application compatibility tests.
- Security regression tests.
- Performance/frame-latency baselines.

### Final product gate
RixuriOS is not complete merely because a desktop appears. Phase 100 closes only when the GUI is an additional reliable product layer over a previously qualified terminal-first OS, with reproducible builds, physical hardware evidence, recovery paths, security tests and documented unsupported configurations.

---

# Cross-Phase Engineering Tracks

## ABI and compatibility
- One authoritative syscall ABI definition.
- Structure packing/alignment tests.
- ELF ABI tests.
- libc compatibility matrix.
- Backward-compatibility policy.

## Reliability
- Timeout ownership.
- Recovery state machines.
- Leak detection.
- Deadlock detection.
- Panic/crash classification.
- Soak testing.

## Security
- Threat model per subsystem.
- Privilege-transition review.
- Parser fuzzing.
- DMA/IOMMU analysis.
- Filesystem race analysis.
- Supply-chain verification.

## Performance
- Boot time.
- Syscall/context-switch latency.
- Scheduler latency.
- Page-fault latency.
- Block I/O latency/throughput.
- Network throughput/latency.
- Filesystem performance.
- GUI frame latency only in Phase 100.

## Hardware evidence
Every physical result records motherboard, firmware, CPU topology, RAM, storage, NIC, USB controller, GPU, boot mode, exact kernel commit, image hash, date, command, raw observation and result.

## Documentation
Every promoted subsystem gets an architecture document, public ABI/API description, ownership/lifetime rules, error/recovery table, diagnostics procedure, test procedure and limitations list.

## What never counts as completion

A printed `OK`, detected PCI device, allocated structure, compiled driver, mocked packet, synthetic keyboard event, test-only ELF loader, fake filesystem, skipped test, source-level claim or undocumented manual observation is not sufficient evidence. Completion requires the real execution path, appropriate negative/recovery coverage, the correct QEMU or physical-hardware evidence class and a recorded checkpoint.

**Authoritative roadmap:** this file only.  
**GUI:** Phase 100 only.  
**Product:** single-user x86_64 desktop PC.  
**Priority:** correctness → recovery → security → hardware evidence → performance → GUI.
