# RixuriOS Master Development Roadmap — v5

**Architecture:** x86_64 / AMD64, 64-bit only  
**Kernel:** freestanding C11/C17 + minimal x86_64 assembly  
**Userspace:** Unix-like, musl/POSIX-oriented, dynamically linked  
**Product:** single-user desktop PC operating system  
**Product principle:** terminal-first, hardware-real, recovery-first  
**GUI:** downstream product layer; never allowed to hide unfinished core OS work

> This is an engineering roadmap, not a feature wishlist. Every phase has implementation scope, dependencies, ABI/data-model work, tests, failure handling, recovery, security review, performance evidence and release evidence. Compilation alone never closes a phase.

## 0. Product Scope

RixuriOS is designed primarily for a **single user on a real x86_64 desktop PC**. The roadmap prioritizes a reliable PC kernel, storage, Ethernet, USB, process/thread model, terminal, developer environment, recovery and eventually a graphical desktop.

Laptop-only product requirements are explicitly out of scope. Battery UI, lid sensors, laptop-specific power profiles and laptop-specific UX are not required. ACPI remains important only where normal desktop-PC operation requires it: firmware discovery, MADT/interrupt routing, MCFG/PCIe discovery, timers, reboot/poweroff, thermal safety and device initialization.

Do not add a subsystem merely because Linux, Windows or another OS has it. Add it when it materially improves the single-user PC product, is architecturally justified, has a clear ownership/ABI model and can be tested honestly.

## 1. Non-negotiable engineering rules

1. Kernel target is x86_64/AMD64 only; no 32-bit kernel architecture.
2. Kernel remains freestanding and never links to glibc, musl, POSIX or Linux kernel APIs.
3. Userspace receives a deliberately documented RixuriOS syscall ABI and compatibility layers.
4. Prefer small interfaces with explicit ownership, lifetime, locking and error semantics.
5. Hardware detection is not driver completion.
6. QEMU and physical hardware are separate evidence classes.
7. Never manufacture packets, disk contents, keyboard events, GPU acceleration or test results.
8. `PASS` is an evidence state, not a string printed by the program.
9. Invalid media, unknown filesystem versions and corrupt metadata must fail safely; never auto-format.
10. Destructive operations require explicit confirmation and disposable test targets.
11. Every subsystem needs positive, negative, boundary, timeout and recovery coverage appropriate to its risk.
12. Security review happens at every privilege, parser, DMA, filesystem and IPC boundary.
13. Historical failures become permanent regression tests.
14. Performance is measured after correctness, never used to hide correctness bugs.
15. Documentation, diagnostics and reproducibility are part of implementation.
16. GUI cannot consume engineering capacity while pre-GUI gates remain open.
17. No phase may silently downgrade a real feature to a fake/demo implementation to obtain a PASS.
18. `SKIP`, `ENOSYS`, `NOT TESTED`, `DEGRADED`, `BLOCKED` and `UNSUPPORTED` remain explicit states.
19. Hardware support is recorded per device/platform, never inferred from source presence.
20. ABI changes require compatibility impact analysis before implementation.

## 2. Universal phase workflow

Every feature follows:

`SPEC → ABI/DATA MODEL → DESIGN → IMPLEMENT → BUILD → UNIT → NEGATIVE → QEMU → INTEGRATION → HARDWARE → REGRESSION → SECURITY → PERFORMANCE → DOCUMENT → CHECKPOINT`

For every implementation task record:

- files created/changed;
- public APIs and ABI changes;
- ownership/lifetime rules;
- locking/context rules;
- error codes and recovery behavior;
- hardware assumptions;
- test commands and expected observations;
- evidence artifact location;
- unresolved limitations.

## 3. Checkpoint vocabulary

- `CP0 SPEC` — requirements and ABI/layout reviewed.
- `CP1 BUILD` — clean compile/link, warnings as errors.
- `CP2 UNIT` — deterministic unit/negative tests.
- `CP3 BOOT` — exercised from real boot path.
- `CP4 INTEGRATION` — real neighboring subsystem path.
- `CP5 HARDWARE` — physical target evidence where applicable.
- `CP6 REGRESSION` — historical failure remains covered.
- `CP7 SECURITY` — privilege/bounds/lifetime/failure review.
- `CP8 PERFORMANCE` — measured baseline and no unacceptable regression.
- `CP9 DOCS` — design, diagnostics and limitations documented.
- `CP10 RELEASE` — reproducible artifact and release evidence.

`BLOCKED`, `NOT TESTED`, `UNSUPPORTED`, `DEGRADED` and `FAIL` are valid states. None equals `COMPLETE`.

---

# PHASE 00 — Project Governance and Reproducible Build

### Build
- Canonical source tree.
- Kernel/userspace/toolchain separation.
- Host and cross-toolchain detection.
- Reproducible compiler flags.
- Dependency pinning.
- Debug/release profiles.
- Symbol/map generation.
- Deterministic disk/ESP image creation.
- Build provenance and artifact hashes.
- Clean-host build documentation.

### Engineering infrastructure
- Coding standards.
- ABI change policy.
- Versioning policy.
- Changelog.
- Architecture decision records.
- Test-result schema.
- Checkpoint ledger.
- Crash-log format.
- Hardware inventory format.
- Known-failure register.
- Release-blocker register.

### Checkpoints
`P00-01` clean build → `P00-02` deterministic image → `P00-03` CI → `P00-04` artifact retention → `P00-05` documentation baseline.

---

# PHASE 01 — UEFI Boot and Firmware Handoff

### Implementation
- Correct EFI table/function-pointer layouts.
- Loaded-image/filesystem access.
- ELF64 validation with overflow checks.
- PT_LOAD allocation/copy/zero-fill.
- Kernel entry contract.
- ACPI RSDP discovery.
- GOP discovery.
- Final UEFI memory map.
- `ExitBootServices()` retry protocol.
- Boot handoff versioning.
- Firmware quirk reporting.

### Debugging
If firmware calls fail, inspect ABI, calling convention, structure packing, stack alignment, function pointers, CR3/page tables and memory corruption before changing random offsets.

### Checkpoints
`P01-01` ELF validation → `P01-02` real UEFI boot → `P01-03` memory map → `P01-04` EBS retry → `P01-05` ACPI/GOP → `P01-06` historical UEFI #UD regression.

---

# PHASE 02 — CPU Bring-up and Memory Safety

### CPU
- CPUID feature inventory.
- MSR access wrappers.
- Control-register policy.
- NX/WP/SMEP/SMAP policy.
- Syscall CPU feature policy.
- Invariant TSC detection.
- CPU feature gating and fallback policy.

### PMM
- UEFI descriptor parser.
- Reserved ranges.
- Frame allocation/free.
- DMA zones/alignment.
- Reference/ownership model.
- Page poisoning/debug allocation modes.

### VMM
- 4/5-level paging according to CPU capability.
- Kernel address space.
- User address spaces.
- Map/unmap/protect.
- Page faults.
- TLB invalidation and shootdown design.
- Huge pages where justified.
- User/kernel permission separation.

### Heap
- Early allocator.
- Size classes/slabs.
- Alignment.
- Overflow checks.
- Guard/debug mode.
- Leak diagnostics.
- Real free/reclaim path; no permanent no-op `kfree` in production.

### Checkpoints
`P02-01` PMM → `P02-02` page tables → `P02-03` permissions → `P02-04` page faults → `P02-05` heap → `P02-06` SMP TLB design → `P02-07` security review.

---

# PHASE 03 — GDT, TSS, IDT, Exceptions and Interrupt Framework

- GDT kernel/user segments.
- TSS and `ltr`.
- IST stacks.
- Complete trap-frame ABI.
- Exceptions 0–31.
- Page-fault diagnostics.
- IRQ stubs.
- Interrupt nesting policy.
- Interrupt-safe logging.
- EOI policy.
- PIC compatibility/disable.
- Spurious IRQ handling.
- Fault-to-process termination policy.

### Checkpoints
`P03-01` exception entry → `P03-02` IST → `P03-03` IRQ entry/return → `P03-04` fault decoding → `P03-05` nested interrupt tests.

---

# PHASE 04 — ACPI, LAPIC, IOAPIC, Timers and SMP

### ACPI
- RSDP checksum.
- XSDT/RSDT parsing.
- MADT CPU/LAPIC/IOAPIC entries.
- Interrupt-source overrides.
- FADT/HPET/MCFG discovery architecture.

### Interrupt routing
- Correct ACPI polarity/trigger translation.
- IOAPIC redirection entries.
- LAPIC/x2APIC where supported.
- MSI/MSI-X groundwork.
- IPI routing.

### Time
- APIC timer.
- HPET fallback where useful.
- PIT compatibility.
- Monotonic clock.
- Wall-clock source architecture.
- Timer wheel/high-resolution timers.

### SMP
- AP trampoline/startup.
- Per-CPU structures.
- CPU online/offline state.
- Barriers and cache coherency assumptions.
- Inter-processor interrupts.
- TLB shootdowns.
- Cross-CPU rendezvous.

### Checkpoints
`P04-01` MADT → `P04-02` IOAPIC → `P04-03` timer IRQ → `P04-04` AP startup → `P04-05` cross-CPU IPI → `P04-06` synchronization regression.

---

# PHASE 05 — Kernel Synchronization, Wait Queues and Workqueues

- Spinlocks.
- IRQ-save locks.
- Mutexes.
- RWLocks.
- Semaphores.
- Condition/wait queues.
- Atomic reference counting.
- Lock ordering rules.
- Deadlock diagnostics.
- Deferred interrupt work.
- Kernel worker threads.
- Cancellation semantics.
- Sleepable vs non-sleepable context annotations.
- Lock contention instrumentation.

### Gate
Every lock documents whether it is legal in interrupt, process and sleepable context.

---

# PHASE 06 — Process, Thread and Scheduler Core

### Process objects
- PID allocation/reuse protection.
- Parent/child relationships.
- Credentials.
- Address-space ownership.
- File descriptor table.
- Signal state.
- Process groups/sessions.
- Exit state/zombies.
- Resource accounting.

### Threads
- Kernel threads.
- User threads.
- Saved CPU context.
- Kernel stack.
- TLS/thread-pointer architecture.
- Thread lifecycle/refcounting.

### Scheduler
- Preemption.
- Per-CPU runqueues.
- Priorities/fairness.
- Sleep/wakeup.
- Timer expiration.
- Idle threads.
- SMP load balancing.
- CPU affinity.
- Starvation diagnostics.
- Context-switch accounting.

### Checkpoints
`P06-01` context switch → `P06-02` timer preemption → `P06-03` sleep/wakeup → `P06-04` multi-CPU scheduling → `P06-05` process lifecycle.

---

# PHASE 07 — Syscall ABI and User/Kernel Boundary

- Stable syscall numbering/version policy.
- Syscall entry/return.
- Kernel stack transition.
- User pointer validation.
- Copy-in/copy-out.
- Canonical-address validation.
- FD validation.
- Errno/error mapping.
- Restartable syscalls.
- Syscall tracing.
- ABI compatibility tests.
- Bad-pointer and malformed-argument tests.

### Initial ABI
`read`, `write`, `openat`, `close`, `stat`, `getpid`, `exit`, `wait`, `mmap`, `munmap`, `mprotect`, `ioctl`, `poll`, `nanosleep`, process creation/exec and signal primitives.

### Gate
A real ring-3 process enters kernel mode, accesses only authorized memory, performs real I/O and returns a defined result.

---

# PHASE 08 — User Address Spaces and ELF64 Execution

- Independent page tables.
- User/kernel split.
- Stack allocation/guard page.
- ELF header/program-header validation.
- PT_LOAD mapping.
- BSS zeroing.
- PIE/non-PIE policy.
- ASLR architecture.
- `argc/argv/envp/auxv`.
- Stack alignment.
- Executable W^X policy.
- `exec` replacement.
- Address-space teardown without leaks.

### Checkpoints
`P08-01` static ELF → `P08-02` malformed ELF rejection → `P08-03` ring-3 start → `P08-04` exec → `P08-05` user memory fault isolation.

---

# PHASE 09 — IPC, Pipes, Signals, Events and Shared Memory

- Anonymous pipes.
- Named pipes/FIFOs.
- Signals and signal masks.
- Signal delivery/return frames.
- Process groups.
- Event objects.
- Poll/select-like waiting.
- Shared memory with explicit permissions.
- Unix-domain socket architecture.
- Descriptor-passing architecture.
- Futex-like userspace synchronization primitive if justified.
- IPC object lifetime/refcounting.

### Gate
Two real processes communicate without bypassing kernel authorization or lifetime rules.

---

# PHASE 10 — PCIe, ACPI MCFG, MMIO, DMA and Device Model

- PCI configuration access.
- PCIe extended configuration.
- MCFG/ECAM.
- Capability traversal.
- BAR sizing/mapping.
- Bus mastering.
- DMA allocation/mapping/unmapping.
- Cache coherency.
- IOMMU/VT-d/AMD IOMMU architecture.
- MSI/MSI-X.
- Driver registration/matching.
- Resource ownership.
- Probe/remove/reset.
- Hotplug state machine.
- Device dependency graph.
- PCI bridge traversal.

### Required identities
- RTL8125 `10EC:8125`.
- RX 6800 XT `1002:73BF`.
- QEMU GPU `1B36:0100`.
- Target NVMe `1CC1:5370` where applicable.

---

# PHASE 11 — Storage Core

### Block layer
- Block device registry.
- Sector/block geometry.
- BIO/request objects.
- Scatter/gather.
- Queue depth.
- Barriers.
- Flush/FUA semantics.
- Completion callbacks.
- Timeout/cancellation.
- Retry policy.
- Error propagation.
- Request prioritization.

### Cache
- Page/buffer cache.
- Dirty tracking.
- Writeback.
- Eviction.
- Coherency with direct I/O.
- Memory-pressure interaction.

### Checkpoints
`P11-01` block API → `P11-02` disposable image → `P11-03` read/write → `P11-04` flush → `P11-05` timeout/recovery.

---

# PHASE 12 — Real NVMe Driver

### Controller
- Reset/disable/enable state machine.
- CAP/VS/CC/CSTS validation.
- Admin queue creation.
- Identify Controller.
- Identify Namespace.
- Namespace lifecycle.

### I/O
- Submission/completion queues.
- Phase tags.
- PRP list construction.
- SGL where needed.
- DMA constraints.
- Interrupt/poll completion.
- Read/write/flush.
- Timeout and controller reset recovery.
- Error status translation.

### Mandatory evidence ladder
`P12-01 PCI → P12-02 BAR → P12-03 RDY → P12-04 Identify Controller → P12-05 Identify Namespace → P12-06 namespace online → P12-07 real read → P12-08 real write → P12-09 real flush → P12-10 timeout/recovery → P12-11 physical hardware regression`.

No controller-detected message may substitute for I/O evidence.

---

# PHASE 13 — VFS and RixFS

### VFS
- Vnode/inode abstraction.
- Dentry/path cache.
- Mount tree/namespaces.
- Superblocks.
- File objects.
- FD tables.
- Path normalization.
- Symlink handling.
- Directory iteration.
- Locks.
- Stat family.
- Rename/unlink/mkdir/rmdir.
- Permissions hooks.
- File offsets and open-file-description semantics.

### RixFS
- Versioned on-disk specification.
- Superblock.
- Inode format.
- Extents/direct data.
- Directories.
- Allocation bitmap/metadata.
- Free-space manager.
- Journal.
- Checksums.
- Orphan/recovery handling.
- Mount/unmount.
- fsck.
- Truncate/read/write.
- Sparse-file support where adopted.

### Critical safety
Unknown, missing, corrupt or incompatible media must return an error/recovery option. **Never silently format.**

---

# PHASE 14 — Time, RTC, Power and PC Hardware Management

- RTC/CMOS abstraction where available.
- Monotonic/realtime clocks.
- Timezone database architecture.
- Sleep/timer APIs.
- ACPI power states required by desktop PCs.
- Reboot/shutdown.
- CPU idle states.
- Thermal safety hooks.
- Platform reset fallback paths.
- Suspend/resume only if a supported desktop target requires it.

Laptop-only battery/lid/power-profile UX is explicitly not required.

---

# PHASE 15 — USB/xHCI Core

- xHCI capability/operational/runtime registers.
- DCBAA.
- Scratchpads.
- Command ring.
- Transfer rings.
- Event ring/ERST.
- TRB cycle ownership.
- Slots.
- Device/input contexts.
- Port reset.
- Address Device.
- Configure Endpoint.
- Control/bulk/interrupt transfers.
- Interrupters/MSI/MSI-X.
- DMA/cache ordering.
- Timeout/reset/recovery.
- Hotplug.

### Regression
Dedicated instrumentation for historical Address Device completion code **11**. Preserve every TRB, slot, context pointer, route string, port state and completion code needed to diagnose it.

---

# PHASE 16 — USB HID, Keyboard, Mouse and Input

- USB descriptor parsing.
- HID descriptor.
- Report descriptor parser.
- Boot protocol.
- Report protocol.
- Interrupt-IN transfers.
- Keycode/modifier state.
- Press/release/repeat.
- Rollover handling.
- Mouse buttons/motion/wheel.
- Hotplug/unplug.
- Input event timestamping.
- Device disconnect cleanup.

### Regression
Real keyboard input must travel through xHCI → USB → HID → input subsystem → TTY. Historical `0x74` (`t`) evidence remains a regression target; synthetic key injection cannot close the hardware gate.

---

# PHASE 17 — TTY, PTY, Console and Terminal Engine

- TTY objects.
- PTY master/slave.
- Canonical/raw modes.
- Termios-like configuration.
- Echo.
- Input/output queues.
- UTF-8.
- ANSI/VT parser.
- Terminal dimensions.
- Controlling terminal.
- Sessions/process groups.
- Flow control.
- Resize events.
- Terminal parser fuzzing.

---

# PHASE 18 — Shell, Job Control and Command Execution

- Shell parser.
- Quoting/escaping.
- Pipelines.
- Redirections.
- Environment expansion.
- Globbing.
- Command lookup.
- Exit status.
- Foreground/background jobs.
- Process groups.
- Controlling terminal.
- Signal-aware job control.
- Command substitution.
- Built-in command framework.

### Gate
A real user can launch, pipe, redirect, background and terminate processes without kernel bypasses.

---

# PHASE 19 — Unix Utilities and Base Userland

Provide a coherent terminal-first base userland covering core filesystem, process, text, diagnostics and system utilities. Utilities must use the documented RixuriOS ABI rather than private kernel interfaces.

### Initial utility families
- Filesystem navigation and manipulation.
- Process inspection/control.
- Text and stream processing.
- Terminal utilities.
- System information.
- Storage inspection.
- Networking diagnostics.
- Basic administrative utilities.
- Environment/configuration tools.

### Gate
Utilities execute as normal userspace programs, return meaningful exit codes, handle errors and do not assume Linux-specific kernel behavior.

---

# PHASE 20 — Users, Groups, Credentials, Sessions and Security Policy

- UID/GID model.
- Supplementary groups.
- Credential propagation.
- File permission model.
- ACL architecture.
- Capability architecture where justified.
- Login/session model.
- Controlling terminal ownership.
- Privilege separation.
- Audit events.
- Security policy configuration.
- ASLR/stack-hardening integration.
- Credential lifetime and revocation.

### Gate
Privilege boundaries are enforced by the kernel and cannot be bypassed by userspace command wrappers.

---

# PHASE 21 — Networking Stack and Device Integration

### Network stack
- Ethernet frame handling.
- ARP.
- IPv4.
- ICMP.
- UDP.
- TCP state machine.
- Retransmission.
- Ordering.
- Flow/window control.
- Checksums.
- Socket API integration.
- Blocking/nonblocking receive/transmit semantics.
- DNS resolver architecture.
- Routing table architecture.

### Drivers
- E1000/QEMU reference path.
- RTL8125 `10EC:8125`.
- Device reset and recovery.
- RX/TX rings.
- Interrupt/MSI-X path where supported.
- DMA mapping.

### Evidence
Loopback success, guest-to-host packets, external connectivity and physical RTL8125 evidence are tracked separately.

---

# PHASE 22 — libc, POSIX Compatibility and C Runtime Surface

### libc
- Freestanding standard headers.
- errno and standard constants.
- String/memory routines.
- stdio streams.
- Formatted I/O.
- scanf-family subset.
- Allocation APIs.
- Environment APIs.
- ctype.
- Time APIs.
- Directory APIs.
- Process APIs.
- POSIX filesystem wrappers.
- Sockets.
- Signals.
- pthread synchronization surface.
- locale/wchar compatibility.
- Utility APIs such as getopt/sysconf/getpagesize.

### POSIX compatibility
Maintain the compatibility matrix with explicit implemented/partial/stub/missing states. ENOSYS is acceptable only where the documented ABI intentionally has no implementation yet and the behavior is tested.

### musl trajectory
Provide a documented syscall/ABI compatibility layer and bootstrap sysroot shape without claiming a full musl port until dynamic linking, TLS, threading and the required syscall surface exist.

### Gate
Static compatibility tests pass in host and QEMU scopes, with deferred dynamic-linking/TLS/hardware evidence explicitly recorded.

---

# PHASE 23 — Dynamic ELF Loader, Shared Libraries and TLS

### ELF dynamic execution
- PT_INTERP handling.
- PT_DYNAMIC parsing.
- Dynamic section validation.
- GOT/PLT relocation.
- REL/RELA processing.
- Symbol lookup.
- `DT_NEEDED` dependency loading.
- SONAME resolution.
- Shared-library search paths.
- Dynamic linker entry.
- Executable/interpreter ABI contract.
- Secure loader failure handling.
- PIE.
- Constructor/destructor ordering.
- Loader dependency cycle handling.

### Dynamic APIs
- `dlopen`.
- `dlsym`.
- `dlclose`.
- `dlerror`.

### TLS
- PT_TLS parsing.
- Static TLS layout.
- Dynamic TLS architecture.
- `%fs` thread pointer setup.
- TLS relocation models.
- Loader/thread handoff contract.
- Thread creation/destruction interaction.

### Gate
A genuinely dynamically linked userspace program loads at boot, resolves shared-library dependencies and accesses TLS without Linux-specific shortcuts.

---

# PHASE 23A — Rix Privileged Command Interface

`rix` is the native RixuriOS privileged/system-management command interface. It is designed around the RixuriOS syscall ABI and privilege model rather than copying Linux `sudo` semantics.

### Core commands

```text
rix status
rix diagnostics
rix shutdown
rix poweroff
rix reboot
rix halt
rix service <name> <action>
rix user <action>
rix group <action>
rix mount <device> <path>
rix umount <path>
rix network <action>
rix storage <action>
```

### Architecture
`user → rix → argument parser → authorization policy → RixuriOS syscall ABI → privileged kernel subsystem`

### Authorization
- UID/GID privilege policy.
- Root/administrator policy.
- Command-specific authorization.
- Kernel-enforced privilege boundaries.
- Fail-closed `EPERM`/authorization errors.
- No implicit privilege through `PATH`.
- Safe environment handling.
- Controlled child environment and FD inheritance.
- Symlink/path-traversal and TOCTOU review.
- Audit records for success and failure.

### Security tests
- Malicious `PATH`.
- Malicious environment variables.
- Relative executable paths.
- Symlink attacks.
- Path traversal.
- TOCTOU races.
- Malformed/oversized arguments.
- Invalid UID/GID values.
- Unauthorized users.
- Repeated failures.
- Inherited descriptors.
- Privilege drop/retention.
- Signal handling.
- Concurrent invocation.
- Shutdown/reboot races.

### Checkpoints
`P23A-01` dispatcher → `P23A-02` authorization → `P23A-03` audit logging → `P23A-04` status → `P23A-05` diagnostics → `P23A-06` shutdown → `P23A-07` reboot → `P23A-08` poweroff → `P23A-09` service interface → `P23A-10` security → `P23A-11` QEMU → `P23A-12` physical evidence where applicable → `P23A-13` documentation.

---

# PHASE 24 — Threads, Futex and Concurrency Runtime

- Kernel thread objects.
- User thread creation.
- Join/detach.
- Thread exit.
- Futex wait/wake/requeue semantics.
- Robust synchronization semantics.
- Per-thread errno/TLS integration.
- pthread runtime integration.
- Scheduler/thread lifetime interaction.
- Cancellation architecture.
- Priority inversion diagnostics.
- Fork/exec behavior in multithreaded processes.
- Thread signal masks and delivery.

### Gate
Multiple user threads execute concurrently with defined synchronization, cleanup and failure behavior.

---

# PHASE 25 — Hardened Memory, Fault Recovery and Process Isolation

- Real kernel heap reclaim.
- Allocator coalescing/slabs.
- Guard pages.
- Stack guards.
- ASLR.
- SMAP/SMEP policy.
- Fault-safe uaccess.
- Page-fault recovery policy.
- Copy-on-write.
- Memory quotas/OOM policy.
- Hardened kernel mappings.
- W^X across appropriate privilege domains.
- Reference-count auditing.
- Use-after-free diagnostics.

---

# PHASE 26 — Advanced VFS, Filesystem Semantics and Recovery

- Symlinks and link-count semantics.
- `fstat`/`lstat` and full stat metadata.
- Timestamps.
- File locking.
- Rename/unlink corner cases.
- Path-cache coherency.
- fsync semantics.
- Crash recovery tests.
- Power-loss regression.
- Filesystem corruption corpus.
- Repair/recovery tooling.
- Mount failure rollback.
- Device disappearance handling.

---

# PHASE 27 — Advanced Networking and System Services

- Complete TCP retransmission/window/congestion behavior.
- DNS resolver.
- Blocking socket waits integrated with scheduler.
- Service manager.
- Logging daemon.
- Time synchronization architecture.
- Network configuration management.
- Local IPC/service sockets.
- IPv6 foundation.
- Socket resource limits.

---

# PHASE 28 — Graphics Foundation and GUI Architecture

GUI is a downstream product layer. It cannot close while required terminal-first, storage, networking, memory, security and hardware gates remain open.

### Graphics
- Framebuffer abstraction.
- Graphics memory management.
- Modesetting architecture.
- GPU command submission architecture.
- Synchronization/fences.
- Display pipeline.
- Cursor.
- Input integration.
- EDID parsing.
- Multi-monitor architecture.

### Windowing
- Compositor.
- Windows/surfaces.
- Event loop.
- Keyboard/mouse routing.
- Terminal emulator client.
- Application lifecycle.

### GPU targets
- QEMU reference graphics path.
- AMD RX 6800 XT `1002:73BF` hardware qualification.
- Acceleration only after stable software rendering/reference path.

---

# PHASE 29 — Package, Toolchain and Developer Environment

- Package format.
- Repository/index format.
- Dependency resolution.
- Signed metadata.
- Compiler/toolchain distribution.
- Debugger support.
- Profiler/tracing tools.
- Developer SDK.
- Reproducible package builds.
- ABI/header versioning.
- Offline build environment.

---

# PHASE 30 — Installer, Recovery Environment and System Lifecycle

- Installer UI/CLI.
- Disk partitioning safety.
- ESP setup.
- Filesystem creation.
- Bootloader installation.
- Upgrade/rollback.
- Recovery environment.
- Rescue shell.
- Backup/restore architecture.
- Migration handling.
- Explicit destructive-operation confirmations.
- Recovery from interrupted installation.

---

# PHASE 31 — Security Hardening and Audit

- Threat model refresh.
- Privilege review.
- Syscall fuzzing.
- Parser fuzzing.
- Filesystem fuzzing.
- Network fuzzing.
- Driver fault injection.
- DMA/IOMMU review.
- Secret handling.
- Secure boot/signing architecture.
- Exploit regression corpus.
- Kernel/user W^X.
- SMEP/SMAP.
- Stack protection.
- ASLR.

---

# PHASE 32 — Performance, Reliability and Soak Testing

- Boot-time measurement.
- Syscall latency.
- Scheduler latency.
- Storage throughput/latency.
- Network throughput/latency.
- Allocator performance.
- Memory pressure.
- Long-run soak tests.
- Power-cycle loops.
- Suspend/resume only where applicable.
- Crash-free endurance criteria.
- Regression threshold policy.

---

# PHASE 33 — Physical Hardware Qualification

### Required evidence
For every supported physical target record:
- Machine identifier.
- Motherboard/firmware.
- CPU.
- Memory.
- Storage controller/device.
- PCI inventory.
- Network controller.
- USB controller.
- GPU/display device.
- Boot mode.
- Kernel configuration.
- Raw serial/framebuffer log.
- Test command.
- Observed result.
- Regression status.
- Exact commit/image hash.

### Initial targets
- RTL8125 `10EC:8125`.
- NVMe device.
- xHCI controller.
- USB HID keyboard/mouse.
- AMD RX 6800 XT `1002:73BF`.
- PCIe/MSI-X behavior.

QEMU results may support development but cannot close physical-hardware gates.

---

# PHASE 34 — Release Candidate, Compliance and Pre-GUI Gate

### Release gates
- All mandatory phases complete or explicitly waived.
- Complete regression suite.
- Security review closed.
- Hardware matrix reviewed.
- Reproducible build artifacts.
- Release notes.
- Known-limitations register.
- Rollback/recovery procedure.
- Signed release artifacts where supported.

### Pre-GUI rule
No GUI milestone may be considered complete until required pre-GUI gates are closed with evidence.

---

# PHASE 35 — Graphical OS Productization

### Product
- Desktop/session model.
- User-facing settings.
- Display manager/login UI if justified.
- Terminal emulator.
- System monitor.
- File manager.
- Networking UI.
- Storage UI.
- Basic power/reboot/shutdown UI.
- Accessibility infrastructure.

### Quality
- Usability testing.
- Crash recovery.
- Update lifecycle.
- Hardware compatibility matrix.
- Documentation.
- Release engineering.

---

# PHASE 36 — Production SMP, CPU Topology and NUMA

### CPU topology
- Enumerate package/core/thread topology from ACPI/CPUID.
- Per-CPU data and per-CPU allocators.
- CPU online/offline state machine.
- CPU affinity and isolation.
- Topology-aware scheduler domains.
- NUMA node discovery and memory locality.
- NUMA-aware allocation policy.

### SMP correctness
- Cross-CPU interrupt delivery.
- IPI types and ownership.
- TLB shootdown batching.
- Remote function calls.
- Stop-the-world coordination.
- RCU-compatible primitives where justified.
- Lock contention instrumentation.
- Interrupt migration rules.
- CPU startup failure recovery.

### Evidence
`P36-01` 2 CPUs → `P36-02` 4 CPUs → `P36-03` concurrent syscall load → `P36-04` TLB shootdown stress → `P36-05` CPU topology report → `P36-06` scheduler affinity stress.

---

# PHASE 37 — Advanced Virtual Memory and Memory Pressure

### VM
- Demand paging.
- Anonymous memory.
- File-backed mappings.
- Copy-on-write.
- Shared/private mapping semantics.
- `mmap`, `munmap`, `mprotect`, `brk` completion.
- Guard regions.
- VMA interval management.
- Page-cache integration.
- Huge-page policy.
- Address-space fragmentation management.

### Reclamation
- Physical page reference accounting.
- Working-set model.
- Reclaim under pressure.
- OOM detection/policy.
- Per-process memory limits.
- Kernel memory accounting.
- Zero-page optimization where useful.
- Dirty-page/writeback interaction.

### Fault handling
- Not-present faults.
- Protection faults.
- COW faults.
- Executable/write violations.
- Stack growth policy.
- Bad-access termination without corrupting the kernel.
- Fault storm throttling.

### Evidence
OOM, fork-heavy COW, mmap fragmentation, unmap/re-map, concurrent page faults and page-cache pressure require deterministic regression tests.

---

# PHASE 38 — Complete Process Model and Job Control

- Process groups.
- Sessions.
- Controlling terminals.
- Orphaned process groups.
- Zombie/reaping correctness.
- Parent-death semantics.
- Exit status propagation.
- `waitpid`/wait-family behavior.
- Foreground/background process control.
- Terminal-generated signals.
- Process resource accounting.
- Per-process limits.
- Environment/argument lifetime guarantees.
- Safe exec transition.
- PID namespace policy only if ever justified by product needs.

### Stress
Thousands of short-lived processes, fork/exec storms, simultaneous exits and parent death must not leak PIDs, file descriptors, VMAs or kernel objects.

---

# PHASE 39 — Signals, Timers and Asynchronous Event Model

### Signals
- Complete signal-set policy.
- Signal masks.
- Pending queues.
- Standard vs queued signals.
- Process-directed and thread-directed delivery.
- Alternate signal stack.
- Signal frame ABI.
- `sigreturn` validation.
- Interrupted syscall restart policy.
- Signal disposition inheritance/reset across exec.
- Synchronous faults.
- Core-dump policy.

### Timers
- Per-process timers.
- Interval timers.
- Realtime timer queue.
- Cancellation races.
- Clock-source correctness.
- Timer overrun accounting.

### Security
Signal frames, user-controlled addresses and return contexts must be validated before restoring privileged CPU state.

---

# PHASE 40 — Threading, Futexes and POSIX Thread Runtime

- Kernel thread lifecycle.
- User thread creation/termination.
- Thread IDs.
- TLS setup/teardown.
- Futex wait/wake/requeue semantics.
- Robust mutex support where justified.
- Thread cancellation model.
- Thread-local errno.
- Thread-local destructors.
- pthread mutex/cond/rwlock/semaphore mapping.
- Scheduler interaction with blocked threads.
- Priority inversion diagnostics.
- Fork/exec behavior in multithreaded processes.
- Thread resource limits.

### Evidence
Run real multithreaded C programs with contention, cancellation, signals, fork, exec, TLS and high thread counts.

---

# PHASE 41 — Dynamic ELF, TLS and Runtime Linker Completion

- PT_INTERP.
- PT_DYNAMIC.
- DT_NEEDED.
- DT_SONAME.
- DT_RPATH/DT_RUNPATH policy.
- Symbol tables and hash tables.
- Relocations.
- REL/RELA handling.
- GOT/PLT.
- Lazy/immediate binding policy.
- Symbol versioning architecture.
- PIE.
- Shared-object load/unload.
- `dlopen`, `dlsym`, `dlclose`, `dlerror`.
- Dependency graph and cycle handling.
- Constructor/destructor ordering.
- TLS program headers.
- Static TLS allocation.
- Dynamic TLS model.
- `%fs` thread pointer setup.
- Auxiliary vector completeness.
- Loader hardening and malformed-ELF corpus.

### Gate
A real dynamically linked userspace binary must execute against RixuriOS's loader and shared libraries without a test-only loader path.

---

# PHASE 42 — musl Port and Full C/POSIX Runtime

- Complete RixuriOS musl syscall layer.
- ABI-specific context/startup files.
- pthread integration.
- TLS integration.
- Signals.
- mmap/brk/mprotect.
- File/directory APIs.
- Sockets.
- Polling.
- Clocks/timers.
- Process APIs.
- Terminal APIs.
- Dynamic loader integration.
- Locale/timezone requirements selected for the product.
- libc conformance test suite.
- errno/thread-safety validation.
- real dynamically linked application tests.

### Compatibility matrix
Track each targeted POSIX/libc interface as `implemented`, `partial`, `unsupported`, `known-broken` or `not-tested`, with a test artifact for every promoted status.

---

# PHASE 43 — VFS Completion, Namespaces and Filesystem Semantics

- `stat`, `fstat`, `lstat` completeness.
- Timestamps and metadata updates.
- Symlinks.
- Hard links.
- Rename atomicity.
- Open flags and mode semantics.
- Advisory locking.
- File leases where justified.
- Mount namespaces only if useful to the product.
- Bind mounts.
- Read-only mounts.
- Mount propagation model.
- Path resolution race resistance.
- Dentry cache invalidation.
- Inode lifetime rules.
- FD inheritance and `CLOEXEC`.
- `dup`/`dup2`/`dup3` semantics.
- Directory FD APIs.
- Path traversal regression suite.

### Recovery
Unmount failures, dirty filesystems, device disappearance and corrupted metadata must have explicit state transitions and diagnostics.

---

# PHASE 44 — RixFS v2: Journaling, Snapshots and Integrity

- Formal on-disk specification.
- Metadata checksums.
- Journal transaction model.
- Ordered/writeback modes.
- Crash-consistency protocol.
- Orphan cleanup.
- Free-space verification.
- fsck repair classes.
- Read-only emergency mount.
- Snapshot metadata.
- Snapshot creation/deletion.
- Copy-on-write snapshot data.
- Quota accounting if needed.
- Sparse files.
- Extended attributes.
- File capabilities metadata where adopted.
- Corruption corpus and repair invariants.
- Journal replay interruption tests.

### Power-loss matrix
Test interruption during allocation, metadata update, rename, truncate, journal commit, fsync and unmount.

---

# PHASE 45 — Networking: Full IPv4/IPv6 and Socket Semantics

### IP
- IPv4 fragmentation/reassembly.
- IPv6.
- ICMP/ICMPv6.
- Neighbor discovery.
- ARP hardening.
- Routing tables.
- Interface configuration.
- MTU handling.
- Multicast basics.
- Loopback completeness.

### Transport
- TCP sequence/ack correctness.
- Retransmission timers.
- Congestion control.
- Receive/send windows.
- Out-of-order queues.
- Duplicate ACK handling.
- FIN/RST state machine.
- TIME_WAIT.
- Keepalive.
- UDP edge cases.

### Sockets
- Blocking/nonblocking.
- Connect/accept/listen.
- Shutdown.
- Socket options.
- Poll/select/epoll-like architecture.
- Unix-domain sockets.
- Descriptor passing.
- Socket lifetime/refcounting.
- Backlog and resource limits.

---

# PHASE 46 — Network Services, DNS, DHCP and Diagnostics

- DHCP client where required.
- DNS stub resolver.
- UDP/TCP DNS fallback.
- IPv6 DNS behavior.
- Resolver configuration.
- Hostname configuration.
- Routing diagnostics.
- Interface statistics.
- Packet counters.
- Socket inspection.
- Network configuration persistence.
- Time synchronization client architecture.
- Network service startup/shutdown ordering.

### Userland
Provide reliable equivalents for basic network administration and troubleshooting rather than hard-coded demo output.

---

# PHASE 47 — Real Hardware Driver Qualification Program

Create a formal driver qualification framework covering:

- PCIe enumeration.
- MSI/MSI-X.
- DMA/IOMMU.
- NVMe.
- SATA/AHCI where supported.
- RTL8125.
- E1000/QEMU NIC.
- xHCI.
- USB HID.
- USB mass storage.
- GPU discovery.
- Audio hardware where selected.
- Thermal/ACPI devices where required for PC safety.

For every driver record:
- PCI identity.
- Firmware requirements.
- DMA constraints.
- Interrupt mode.
- Reset procedure.
- Timeout values.
- Error recovery.
- Hotplug behavior.
- Known limitations.
- Physical PASS/FAIL evidence.

---

# PHASE 48 — GPU, Display and Window-System Foundation

### Display
- Framebuffer abstraction.
- EDID parsing.
- Connector/mode discovery.
- Multi-monitor model.
- Hotplug detection.
- Pixel formats.
- Scanout buffers.
- Cursor planes where useful.

### GPU
- PCI discovery.
- VRAM/GTT model.
- Command submission architecture.
- Synchronization/fences.
- Memory management.
- Reset/recovery.
- Hardware acceleration only after stable modesetting.
- Software-rendering reference path.

### Window system
- Compositor architecture.
- Surfaces/buffers.
- Input routing.
- Focus/activation.
- Clipboard/selection.
- Cursor.
- Damage tracking.
- Accessibility hooks.

---

# PHASE 49 — Audio and Multimedia Subsystem

- Audio device abstraction.
- Codec discovery.
- Playback/capture streams.
- Ring buffers.
- Sample-rate conversion policy.
- Mixer/volume model.
- Underrun/overrun recovery.
- Device hotplug.
- Latency measurement.
- Userland audio service.
- Basic media timing primitives.

No audio device is marked supported from PCI detection alone.

---

# PHASE 50 — PC Platform Power, Thermal and Firmware Services

This phase is deliberately **desktop-PC focused**, not laptop focused.

- ACPI namespace evaluation framework.
- Power button handling where exposed.
- Thermal zones required for hardware safety.
- Fan/thermal control only where safe and useful on the target PC.
- CPU idle-state architecture.
- Reboot/poweroff reliability.
- Wake/reset behavior where applicable.
- Device initialization ordering.
- Firmware failure diagnostics.
- Suspend/resume only if explicitly required by a supported desktop target.

No battery, lid, laptop power-profile or laptop-specific UX requirement is part of the RixuriOS product definition.

---

# PHASE 51 — Security Architecture and Privilege Isolation

- User/kernel W^X.
- SMEP/SMAP enforcement.
- NX everywhere practical.
- Hardened usercopy.
- Pointer/integer overflow auditing.
- Stack protection.
- Kernel stack guards.
- KASLR architecture where practical.
- Userspace ASLR.
- Capability-oriented permission model where adopted.
- Fine-grained privileged operations.
- Secure path resolution.
- TOCTOU-resistant authorization.
- FD and object lifetime hardening.
- Syscall fuzzing.
- Driver attack-surface review.
- DMA isolation.
- IOMMU policy.
- Privilege-boundary regression corpus.

### Security evidence
Every mitigation needs a positive test, a bypass attempt and a documented hardware dependency.

---

# PHASE 52 — Cryptography, Entropy and Trust Infrastructure

- Hardware entropy sources.
- Kernel CSPRNG.
- Early-boot entropy strategy.
- Cryptographic primitive library boundary.
- Constant-time implementation rules.
- Key material lifetime/zeroization.
- Secure storage architecture.
- Hash/integrity primitives.
- Authenticated update metadata.
- Certificate/time validation dependencies.
- Secure random userland API.
- Key generation and rotation policy.

Do not roll custom cryptography when a reviewed implementation can be reused safely.

---

# PHASE 53 — Secure Boot, Measured Boot and Update Trust

- UEFI Secure Boot compatibility.
- Signed boot artifacts.
- Signature verification.
- Key rotation policy.
- Revoked-key handling.
- Measured boot architecture where TPM is available.
- Boot-chain measurement logs.
- Anti-rollback metadata.
- Signed kernel/modules/userspace packages.
- Recovery key path.
- Offline verification tools.

Failure must stop or enter an explicit recovery path rather than silently booting an untrusted image.

---

# PHASE 54 — Service Manager, Init and System Lifecycle

- PID 1 design.
- Service descriptors.
- Dependency graph.
- Ordering constraints.
- Restart policy.
- Crash-loop detection.
- Readiness/liveness state.
- Privilege dropping.
- Service sandboxing.
- Environment policy.
- Resource limits.
- Logging integration.
- Shutdown transaction graph.
- Recovery/single-user mode.
- Boot target selection.
- Service failure propagation.

### `rix` integration
`rix service`, `rix status`, `rix diagnostics`, `rix reboot`, `rix poweroff` and related commands must call real kernel/service interfaces with authorization checks.

---

# PHASE 55 — procfs/sysfs/devfs and System Introspection

- `/proc` process model.
- CPU information.
- Memory information.
- Mounts.
- Open files where safe.
- Process status.
- `/sys` device hierarchy.
- Driver binding state.
- Power state.
- `/dev` device nodes.
- Major/minor allocation.
- Uevent/hotplug architecture.
- Stable hardware identifiers.
- Kernel statistics.
- Network statistics.
- Storage statistics.

Diagnostics must expose real state and never fabricate support.

---

# PHASE 56 — Observability, Crash Dumps, Tracing and Debugging

### Logging
- Structured kernel logs.
- Severity levels.
- Timestamps.
- CPU/thread/process IDs.
- Rate limiting.
- Persistent crash logs.
- Per-subsystem log domains.

### Crash analysis
- Panic reason.
- Register dump.
- Stack trace.
- Page-fault metadata.
- Loaded image/module list.
- Recent scheduler/IRQ history.
- Storage/network/USB controller state snapshots.
- Crash dump persistence and recovery.

### Tracing
- Syscall tracing.
- Scheduler tracing.
- Lock contention.
- IRQ latency.
- Block I/O latency.
- Network latency.
- Userspace profiling hooks.
- Trace-buffer overflow behavior.

---

# PHASE 57 — Testing, Fuzzing and Fault Injection Platform

### Automated tests
- Unit tests.
- Kernel component tests.
- Userspace ABI tests.
- libc conformance.
- Filesystem tests.
- Networking tests.
- Driver tests.
- Boot tests.
- Power-cycle tests.
- Installer/recovery tests.

### Fuzzing
- ELF parser.
- Filesystem metadata.
- Path resolver.
- Syscall arguments.
- USB descriptors/HID reports.
- PCI capabilities.
- ACPI tables.
- Network packets.
- Terminal escape sequences.
- Config/package metadata.

### Fault injection
- Allocation failure.
- DMA mapping failure.
- IRQ loss.
- Device timeout.
- Controller reset.
- Corrupted media.
- Packet loss/reordering.
- CPU starvation.
- Power-loss simulation.
- Filesystem journal interruption.

---

# PHASE 58 — Toolchain, SDK, Package Manager and Developer Platform

- Reproducible cross compiler.
- Sysroot generation.
- libc/toolchain bootstrap.
- Assembler/linker/binutils or LLVM integration policy.
- Headers and ABI versioning.
- Debugger support.
- Symbol server/debug package policy.
- Package format.
- Package metadata.
- Dependency solver.
- Repository metadata.
- Signed package verification.
- Install/remove/upgrade/rollback.
- Developer SDK image.
- Application template.
- Documentation generator.
- Offline developer environment.

### Goal
A third-party developer should be able to build a real dynamically linked RixuriOS program from a clean host without manually patching the kernel tree.

---

# PHASE 59 — Installer, Recovery, A/B Updates and Disaster Recovery

### Installer
- Hardware discovery.
- Disk selection safeguards.
- Partitioning.
- ESP creation/validation.
- RixFS creation only after explicit confirmation.
- Bootloader installation.
- Initial user creation.
- Network setup.
- Timezone/locale setup.
- Installation log.

### Recovery
- Standalone recovery environment.
- Filesystem check/repair.
- Offline logs.
- Account recovery policy.
- Boot configuration repair.
- Rollback selection.
- Hardware diagnostics.
- Safe read-only mode.

### Updates
- Atomic update staging.
- A/B system slots where adopted.
- Signed manifests.
- Rollback on failed boot.
- Persistent health marker.
- Interrupted-update recovery.

---

# PHASE 60 — Release Engineering, Compatibility and RixuriOS 1.0

### Qualification
- Boot matrix.
- CPU matrix.
- Motherboard/firmware matrix.
- GPU matrix.
- NVMe/SATA/storage matrix.
- NIC matrix.
- USB controller matrix.
- Display/audio matrix.
- QEMU version matrix.
- Supported/unsupported device inventory.

### Release gates
- Reproducible release image.
- Clean-host rebuild.
- Deterministic hashes.
- Complete changelog.
- Known-issues database.
- Security review.
- ABI compatibility review.
- Filesystem compatibility review.
- Installer/recovery test.
- Update/rollback test.
- Hardware qualification evidence.
- Performance baseline.
- Long-duration soak test.

### Final product criteria
RixuriOS 1.0 is not declared complete because it boots or displays a desktop. It requires stable kernel/user ABI, functional VM and reclamation, process/thread/signal model, dynamic ELF/TLS/libc runtime, robust VFS/storage, usable networking/DNS, qualified core hardware, tested security boundaries, recovery/update paths, reproducible toolchain, application compatibility and physical-hardware evidence.

---

# PHASE 61 — PC Platform Baseline and Hardware Compatibility Matrix

- Define supported desktop-PC hardware envelope.
- CPU generation/feature compatibility matrix.
- AMD/Intel CPU validation.
- UEFI firmware compatibility classes.
- PCIe topology capture.
- NVMe/SATA/USB/NIC/GPU inventory.
- Unsupported-device reporting.
- BIOS/UEFI quirk database.
- Reproducible hardware inventory reports.
- No generic `supported` flag without evidence.
- Boot-mode compatibility records.
- Firmware-version regression records.

---

# PHASE 62 — SATA/AHCI Storage Driver

- AHCI controller discovery.
- HBA reset/initialization.
- Command list/FIS handling.
- DMA setup.
- SATA identify.
- Read/write/flush.
- NCQ architecture.
- Timeout and port reset.
- Hotplug where applicable.
- Bad-sector/error propagation.
- QEMU and physical SATA qualification.
- Controller fault recovery.

---

# PHASE 63 — Generic Block Devices and Storage Multiplexing

- Unified block-device registration.
- NVMe/AHCI/virtual disk adapters.
- Partitions and partition-table parsing.
- GPT validation and backup-header handling.
- Protective MBR handling.
- Block-device naming.
- Request scheduling.
- Queue depth control.
- Device disappearance handling.
- Storage diagnostics and health reporting.
- Flush/barrier propagation.
- Block-layer tracing.

---

# PHASE 64 — Partitioning, Formatting and Disk Administration

- GPT creation/editing.
- Partition resize rules.
- Filesystem creation tools.
- Explicit destructive-operation confirmation.
- Dry-run mode.
- Disk geometry reporting.
- Free-space inspection.
- Alignment validation.
- Recovery from interrupted partition operations.
- Never auto-format an unknown disk.
- Backup/restore of partition metadata.
- Recovery from damaged GPT primary/backup headers.

---

# PHASE 65 — File I/O Completion and POSIX Semantics

- Open flags.
- Append and truncation semantics.
- `pread`/`pwrite`.
- `readv`/`writev`.
- `fsync`/`fdatasync`.
- `fcntl` operations.
- Advisory locks.
- Directory FD operations.
- `O_CLOEXEC` and `O_NONBLOCK`.
- Accurate errno behavior.
- Concurrent file-offset correctness.
- Short read/write semantics.
- EINTR behavior.
- Atomic append semantics.
- File descriptor inheritance tests.

---

# PHASE 66 — Unix Compatibility Surface Expansion

- Complete selected path/file APIs.
- `access`, `chdir`, `fchdir`, `getcwd`.
- `chmod`, `fchmod`, `chown` policy.
- Link/unlink/rename semantics.
- Directory stream runtime.
- Environment APIs.
- Resource-limit APIs.
- Hostname/system-information APIs.
- Compatibility tests against real applications.
- Unsupported interfaces documented instead of falsely emulated.
- Error/errno compatibility corpus.
- ABI version compatibility tests.

---

# PHASE 67 — Shell Completion and Interactive Userland

- Robust command parser.
- Quoting and escaping.
- Environment expansion.
- Command substitution.
- Pipelines.
- Redirections.
- Background jobs.
- Signals and terminal control.
- History.
- Line editing.
- Tab completion.
- Built-in command framework.
- Shell startup files.
- Exit-status propagation.
- Parser fuzzing.

---

# PHASE 68 — Core Userland Utilities

Implement real utilities rather than demonstration commands:

- `cat`, `cp`, `mv`, `rm`, `mkdir`, `rmdir`.
- `ls`, `find`, `grep`, `sort`, `head`, `tail`.
- `printf`, `echo`, `env`, `pwd`, `kill`.
- `ps`, `mount`, `umount`, `df`, `du`.
- `dmesg`/kernel-log reader.
- `ip`/network diagnostics.
- `shutdown`/`reboot` through authorized interfaces.
- Consistent exit status and stderr behavior.
- `chmod`/`chown` tools where supported.
- File inspection and checksum utilities.
- Basic archive/backup tooling where justified.

---

# PHASE 69 — Process Resource Limits and Accounting

- CPU-time accounting.
- Address-space limits.
- Open-FD limits.
- Process/thread limits.
- Locked-memory limits if supported.
- Per-user accounting.
- Kernel-object accounting.
- OOM diagnostics.
- Runaway-process protection.
- Resource usage API.
- Scheduler statistics.
- Per-process I/O accounting.
- Per-process memory accounting.

---

# PHASE 70 — Device Model and Driver Lifecycle 2.0

- Formal bus/device/driver objects.
- Driver matching.
- Dependency ordering.
- Probe/remove lifecycle.
- Reset/recovery callbacks.
- Device reference counting.
- Deferred probing.
- Device ownership.
- Hotplug events.
- Driver diagnostics.
- Resource release verification.
- Device shutdown ordering.
- Driver failure isolation.

---

# PHASE 71 — PCIe Advanced Features

- PCI capability parser hardening.
- MSI/MSI-X completion.
- PCIe AER architecture.
- Error containment.
- Link-status diagnostics.
- BAR conflict detection.
- Bus numbering.
- Bridge traversal.
- Multifunction devices.
- PCI reset mechanisms.
- Device-level fault recovery.
- PCIe hotplug architecture where useful.
- AER fault-injection tests.

---

# PHASE 72 — IOMMU and DMA Isolation

- AMD IOMMU and Intel VT-d architecture.
- Device/domain mapping.
- DMA aperture control.
- Identity-vs-translated DMA policy.
- Invalidation.
- Interrupt remapping where supported.
- DMA fault reporting.
- Driver isolation.
- Bounce-buffer fallback.
- Security tests for malicious DMA assumptions.
- IOMMU enable/disable fallback policy.
- DMA lifetime leak tests.

---

# PHASE 73 — USB Device Framework Beyond HID

- USB hub support.
- Enumeration tree.
- Device/configuration/interface/endpoint objects.
- Class-driver registration.
- Control transfer framework.
- Bulk transfer framework.
- Interrupt transfer framework.
- Isochronous architecture.
- Disconnect/reconnect races.
- Device reset/recovery.
- Descriptor validation/fuzzing.
- Hub port power/reset handling where available.

---

# PHASE 74 — USB Mass Storage and Removable Media

- BOT protocol.
- SCSI command layer.
- Inquiry/capacity/read/write.
- Sense data.
- Removable media detection.
- Filesystem remount behavior.
- Safe unplug handling.
- Timeout/reset.
- Corrupted-media tests.
- Physical USB storage qualification.
- Write-protect handling.
- Device disappearance during I/O.

---

# PHASE 75 — Networking Driver Completion

- RTL8125 real RX/TX path.
- E1000 virtual and physical validation.
- Descriptor ownership.
- Interrupt moderation policy.
- DMA mapping.
- Ring reset.
- Link negotiation reporting.
- MTU configuration.
- Packet statistics.
- Link-down/recovery.
- Sustained traffic stress.
- RX/TX starvation recovery.
- Driver watchdog.

---

# PHASE 76 — Network Security and Robustness

- ARP poisoning resistance where applicable.
- Malformed IPv4/IPv6 packet handling.
- TCP state-machine hardening.
- Socket lifetime races.
- SYN/resource exhaustion policy.
- Ephemeral-port allocation.
- Firewall architecture if selected.
- Per-process socket ownership.
- Network syscall fuzzing.
- Packet-loss/reordering tests.
- Route validation.
- ICMP abuse/resource limits.
- Network namespace only if product requirements justify it.

---

# PHASE 77 — IPv6 and Modern Network Features

- IPv6 address configuration.
- Neighbor discovery.
- Router advertisements.
- Link-local addresses.
- Dual-stack sockets.
- IPv6 routing.
- ICMPv6.
- PMTU handling.
- IPv6 DNS behavior.
- IPv4/IPv6 compatibility tests.
- Address lifetime handling.
- Temporary/privacy address policy if desired.

---

# PHASE 78 — Timekeeping and Clock Correctness

- Monotonic clock validation.
- Realtime clock discipline.
- TSC synchronization across CPUs.
- Clocksource selection.
- Timer drift measurement.
- Sleep/wakeup accuracy.
- Timeout monotonicity.
- Filesystem timestamp correctness.
- 2038-independent internal design.
- Time-related syscall conformance.
- Clock adjustment policy.
- NTP-like synchronization client if required.

---

# PHASE 79 — Kernel Debugger and Remote Debug Infrastructure

- Panic debugger entry.
- Register inspection.
- Stack unwinding.
- Symbol lookup.
- Breakpoint/watchpoint architecture.
- GDB-compatible remote protocol where practical.
- Kernel/user address inspection.
- Thread inspection.
- Deadlock inspection.
- Crash-to-debugger workflow.
- Debug symbol packaging.
- Safe debugger behavior on corrupted state.

---

# PHASE 80 — Kernel Sanitizers and Memory Debugging

- Allocation poisoning.
- Redzones/guard pages.
- Use-after-free detection.
- Double-free detection.
- Slab consistency checks.
- Reference-count diagnostics.
- Lock misuse detection.
- Interrupt-context assertions.
- Usercopy boundary instrumentation.
- Debug builds separated from production builds.
- Leak reporting.
- Fault-injection allocator.
- Memory corruption signatures.

---

# PHASE 81 — Concurrency Verification

- Lock dependency graph.
- Race-oriented stress tests.
- Scheduler perturbation.
- Randomized wake ordering.
- Interrupt timing injection.
- SMP stress.
- Futex contention.
- Filesystem concurrent access.
- Network concurrent sockets.
- Deadlock watchdog.
- Priority inversion tests.
- CPU affinity stress.
- Preemption storm tests.

---

# PHASE 82 — Long-Running Reliability / Soak Program

- 24-hour QEMU stress.
- Multi-day physical-PC stress.
- Repeated reboot cycles.
- Repeated mount/unmount.
- Repeated device reset.
- Network soak.
- Storage soak.
- Process churn.
- Memory-pressure soak.
- Crash-rate tracking.
- Leak-rate tracking.
- Thermal stability observation where relevant.
- Automatic artifact collection after failure.

---

# PHASE 83 — Application Compatibility Qualification

Create a curated real-application suite:

- Static C programs.
- Dynamically linked C programs.
- pthread applications.
- Terminal applications.
- Network clients.
- Filesystem-heavy programs.
- Build tools.
- Text-processing utilities.
- Shell scripts.
- Representative third-party software.
- Long-running daemons/services.
- Failure/restart scenarios.

Each application gets a reproducible PASS/FAIL record and dependency report.

---

# PHASE 84 — Build-System and Reproducibility 2.0

- Clean-room builds.
- Pinned compiler/tool versions.
- Source archive reproducibility.
- Deterministic ELF output where practical.
- Deterministic filesystem images.
- Build provenance.
- Generated-file tracking.
- Dependency license inventory.
- Offline build mode.
- Release artifact hashes.
- Build cache correctness.
- Host contamination detection.
- Reproducibility comparison between independent builds.

---

# PHASE 85 — Package Ecosystem and Repository Infrastructure

- Package metadata schema.
- Dependency constraints.
- ABI compatibility fields.
- Package signatures.
- Repository index.
- Mirror support.
- Atomic installation.
- Transaction rollback.
- Orphan dependency cleanup.
- Package verification before execution.
- Package conflict detection.
- Repository freshness/metadata validation.
- Offline package installation.

---

# PHASE 86 — System Recovery and Forensic Mode

- Boot failure diagnosis.
- Safe/single-user shell.
- Read-only filesystem recovery.
- Damaged RixFS inspection.
- Kernel crash-log extraction.
- Hardware inventory extraction.
- Network-disabled recovery mode.
- Emergency account recovery policy.
- Recovery image integrity verification.
- Evidence-preserving diagnostics.
- Boot configuration repair.
- Storage recovery tooling.
- Exportable diagnostic bundle.

---

# PHASE 87 — Update, Rollback and Compatibility Policy

- Versioned kernel ABI policy.
- Userspace ABI compatibility.
- Package dependency migration.
- Atomic system update.
- Rollback point creation.
- Interrupted-update recovery.
- Bootable previous version.
- Database/config migration rollback.
- Update verification.
- Explicit incompatible-update refusal.
- Configuration schema migration.
- Recovery after failed post-install scripts.

---

# PHASE 88 — Desktop PC Productization

- Desktop login/session startup.
- Terminal-first default environment.
- Optional graphical session.
- Display manager only if justified.
- Desktop configuration storage.
- Keyboard/mouse configuration.
- Multi-monitor configuration.
- Application launching.
- Clipboard.
- Basic notifications.
- Clean shutdown/reboot UX.
- Crash recovery UX.
- System diagnostics UI only after reliable CLI equivalents exist.

---

# PHASE 89 — Release Candidate Engineering

- Complete regression matrix.
- Zero unexplained kernel panics.
- Zero known data-corruption paths.
- No fake hardware PASS records.
- No silent filesystem formatting.
- No unresolved critical privilege bypass.
- Boot/install/recovery verification.
- Physical-PC qualification report.
- Compatibility report.
- Performance baseline.
- Release blocker list.
- Reproducibility verification.
- Update/rollback verification.
- Long-duration stability evidence.

---

# PHASE 90 — RixuriOS 1.0 Qualification and Long-Term Maintenance

### 1.0 gates
- Reproducible release artifact.
- Signed release metadata where adopted.
- Documented supported hardware.
- Documented unsupported hardware.
- Documented syscall ABI.
- Documented userspace ABI.
- Documented filesystem format.
- Recovery procedure.
- Installation procedure.
- Upgrade/rollback procedure.
- Security baseline.
- Performance baseline.
- Application compatibility report.
- Physical hardware evidence.
- Known-issues register.
- Stable release branch.

### Maintenance
- Stable branch policy.
- Security-fix process.
- Regression-test preservation.
- ABI deprecation policy.
- Filesystem compatibility policy.
- Hardware-quirk maintenance.
- Release cadence.
- Crash-report triage.
- Long-term documentation.
- Backport policy.

---

# PHASE 91 — Advanced Kernel Architecture and RCU

- Formal kernel execution-context model.
- RCU-style primitives only where they materially simplify read-mostly paths.
- Grace-period implementation and validation.
- Per-CPU deferred reclamation.
- Lock hierarchy verification.
- Lock-free/atomic structures only where justified.
- Memory-ordering documentation.
- Cross-CPU memory-barrier tests.
- Preemption/interrupt interaction rules.
- Scheduler and RCU interaction.
- RCU stall diagnostics.
- Safe object reclamation after readers exit.

---

# PHASE 92 — Advanced Scheduler and CPU Resource Control

- Scheduler policy abstraction.
- Priority classes.
- Fairness measurement.
- CPU affinity APIs.
- CPU isolation where useful.
- Runtime quotas.
- Scheduler latency tracing.
- Wakeup locality.
- NUMA-aware placement.
- Priority inversion mitigation.
- Starvation detection.
- Scheduler fuzzing.
- Realtime behavior only if justified by product requirements.

---

# PHASE 93 — Kernel Memory Allocation 2.0

- Slab/SLUB-like allocator architecture if justified.
- Per-CPU caches.
- NUMA-aware allocation.
- Fragmentation metrics.
- Large-allocation strategy.
- DMA-capable allocation classes.
- Allocation flags/context rules.
- Emergency reserves.
- OOM-safe kernel paths.
- Quarantine/debug allocation mode.
- Heap corruption diagnostics.
- Allocation tracing.

---

# PHASE 94 — Advanced Block I/O and Storage Reliability

- I/O scheduler policy.
- Queue fairness.
- Multi-queue storage.
- Request merging.
- Writeback throttling.
- Read-ahead policy.
- Direct-I/O architecture where useful.
- Storage timeout hierarchy.
- Device health/error statistics.
- Persistent error logs.
- Bad-block handling policy.
- Recovery from controller resets.

---

# PHASE 95 — Storage Integrity and Data Protection

- End-to-end metadata integrity.
- Checksummed critical structures.
- Scrub/verification tooling.
- Offline integrity scan.
- Recovery-point management.
- Snapshot consistency tests.
- Backup verification.
- Restore verification.
- Interrupted-write corpus.
- Data-corruption detection rather than silent repair.
- Explicit user-facing recovery states.

---

# PHASE 96 — Advanced Networking and Network Management

- Route management API.
- Interface lifecycle.
- Link-state events.
- Network configuration persistence.
- DNS cache policy.
- Resolver failure diagnostics.
- Socket statistics.
- Connection tracking architecture if firewall requires it.
- Firewall rule model if selected.
- Packet filtering hooks.
- Rate limiting.
- Network service dependency graph.

---

# PHASE 97 — Hardware Error Recovery and Reliability

- PCIe AER recovery.
- NVMe controller reset/recovery.
- NIC watchdog/recovery.
- xHCI controller recovery.
- GPU reset architecture where feasible.
- DMA fault handling.
- Device disappearance/reappearance.
- Driver crash containment.
- Error escalation policy.
- Persistent hardware fault records.
- Recovery tests for every supported driver.

---

# PHASE 98 — Security Policy, Sandboxing and Capabilities

- Fine-grained privilege model.
- Capability objects where justified.
- Sandboxed service model.
- Resource restrictions.
- Syscall allow/deny policy only where useful.
- File/path capability policy.
- Device access policy.
- Network privilege policy.
- Privilege-drop verification.
- Sandbox escape regression suite.
- Security audit log integrity.

---

# PHASE 99 — Supply Chain and Trusted Build Infrastructure

- Source provenance.
- Reproducible compiler chain.
- Toolchain bootstrap verification.
- Dependency integrity.
- Signed source/release metadata.
- Build-environment attestation where useful.
- Vulnerability tracking for bundled dependencies.
- License compliance.
- Offline verification.
- Release signing ceremony/documentation.

---

# PHASE 100 — RixuriOS Platform Stability Gate

This is the first major post-1.0 engineering gate and establishes that the OS can evolve without destroying compatibility.

- Stable syscall ABI policy.
- Stable userspace ABI policy.
- Stable RixFS compatibility policy.
- Driver ABI/module policy if modules are adopted.
- Deprecation process.
- Migration tools.
- Backward compatibility tests.
- Upgrade-from-previous-release tests.
- Recovery-from-broken-upgrade tests.
- Multi-release regression corpus.

### Gate
No new feature is accepted if it silently breaks documented stable interfaces without an explicit migration/deprecation path.

---

# Cross-Phase Engineering Tracks

These tracks run continuously from Phase 00 onward and cannot be postponed to the end.

## A. ABI and Compatibility
- Syscall ABI versioning.
- Structure packing/alignment review.
- Userspace ABI tests.
- ELF ABI tests.
- libc compatibility matrix.
- Backward-compatibility policy.
- Deprecation/migration tooling.

## B. Reliability
- Watchdogs where appropriate.
- Timeout ownership.
- Recovery state machines.
- Leak detection.
- Deadlock detection.
- Panic/crash classification.
- Soak testing.
- Historical failure preservation.

## C. Security
- Threat model per subsystem.
- Privilege transition review.
- Parser fuzzing.
- DMA threat analysis.
- Filesystem race analysis.
- Syscall attack-surface review.
- Supply-chain verification.
- Security regression corpus.

## D. Performance
- Boot time.
- Syscall latency.
- Context-switch latency.
- Scheduler latency.
- Page-fault latency.
- Block I/O throughput/latency.
- Network throughput/latency.
- Filesystem throughput.
- Graphics frame latency.
- Memory overhead.

Performance numbers must include hardware/QEMU configuration and test method.

## E. Hardware Evidence
Maintain a machine-readable inventory containing:
- motherboard;
- firmware version;
- CPU topology;
- RAM;
- storage controller/device IDs;
- NIC IDs;
- USB controller IDs;
- GPU ID;
- audio device;
- ACPI capabilities;
- boot mode;
- exact kernel commit;
- exact image hash;
- test date;
- result;
- failure signature.

## F. Documentation
Every promoted subsystem gets:
- architecture document;
- public ABI/API document;
- error/recovery table;
- diagnostic commands;
- test instructions;
- known limitations;
- evidence links;
- compatibility status.

## G. Test Evidence Integrity
Never turn an unavailable test into a PASS by:
- adding `SKIP` while claiming success;
- replacing hardware with a mock without changing the evidence class;
- printing a success string without performing the operation;
- disabling the failing code path;
- returning a fake packet/storage result;
- weakening validation only for the test;
- suppressing an error from the checkpoint log.

## H. Change Management
Every substantial change should identify:
- affected phases;
- dependency changes;
- ABI impact;
- security impact;
- performance impact;
- regression tests;
- hardware impact;
- migration requirements;
- rollback strategy.

---

# Global Definition of Done

A phase is complete only when its required implementation exists on the real code path, the ABI/data model is documented, positive tests pass, negative/boundary behavior is tested, recovery behavior is defined, QEMU evidence exists where applicable, physical hardware evidence exists where applicable, historical regressions remain covered, security review is performed, performance is measured where relevant, documentation is updated and the checkpoint ledger records the evidence.

A phase is **not** complete because:

- the project compiles;
- a symbol exists;
- a driver detects hardware;
- a command prints `OK`;
- a test is skipped;
- a mock returns expected data;
- QEMU passes when physical hardware is required;
- source inspection appears correct;
- a feature exists behind an unused code path;
- an unsupported operation returns success.

## Required evidence classes

`UNIT` → deterministic component behavior  
`QEMU` → virtual-machine integration  
`PHYSICAL` → real PC hardware  
`REGRESSION` → historical failures remain prevented  
`SECURITY` → abuse/failure/bypass attempts  
`PERFORMANCE` → measured behavior and baseline  
`RELEASE` → reproducible artifact and compatibility evidence

## Final product philosophy

RixuriOS should first become a **reliable operating system for one person using a real desktop PC**. The terminal, process model, memory system, storage, filesystem, Ethernet, USB, recovery tooling, security boundaries and developer environment are the foundation. Graphics are built on top of that foundation, not used to disguise missing kernel functionality.
