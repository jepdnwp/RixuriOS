# RixuriOS Master Development Roadmap — v8

**Architecture:** x86_64 / AMD64 only  
**Kernel:** freestanding C11/C17 + minimal x86_64 assembly  
**Userspace:** Unix-like, musl/POSIX-oriented, dynamically linked  
**Product:** single-user desktop PC operating system  
**Primary software goal:** a single owner can take an open-source project from GitHub, resolve its dependency graph recursively, adapt/port each component to the RixuriOS API/ABI, build and install the complete result.  
**GUI:** **PHASE 100 ONLY — absolutely last**

> This is the single authoritative roadmap. RixuriOS is optimized for one owner using one x86_64 desktop PC. The software ecosystem is not based on manually porting every application into a permanent hand-maintained package list. Its long-term goal is a recursive source-to-native pipeline: **GitHub repository → project analysis → dependency graph → recursive dependency porting → RixuriOS API adaptation → build → test → package → install**.

---

# 1. PRODUCT DEFINITION

RixuriOS is a single-user, terminal-first, hardware-real, recovery-first x86_64 desktop OS. The owner should eventually be able to provide a GitHub repository and have RixuriOS determine what is needed to build it, obtain its source dependencies, adapt incompatible APIs, build dependencies before dependents, package the result and install it.

The kernel remains a real freestanding kernel. The automatic software-porting system lives in userspace/build infrastructure and must never weaken kernel/user security boundaries.

### Explicitly not primary scope

- Multi-user enterprise administration.
- LDAP/Active Directory.
- Multi-seat desktops.
- Server/cluster/cloud orchestration.
- Laptop battery/lid/power-profile UX.
- Non-x86_64 architectures.
- A Linux binary-compatibility layer as a product requirement.
- A GUI before Phase 100.

Unix UID/GID and permission primitives may still exist because software and filesystem correctness benefit from them; the product remains centered on one human owner.

---

# 2. NON-NEGOTIABLE RULES

1. x86_64 only.
2. Kernel is freestanding and never links against glibc/musl/POSIX.
3. Userspace uses a documented RixuriOS syscall ABI.
4. Detection is never counted as implementation.
5. QEMU and physical hardware evidence are separate.
6. No fake hardware, packets, disk results or synthetic PASS records.
7. PASS requires reproducible evidence.
8. Unknown/corrupt media is never silently formatted.
9. Destructive operations require explicit confirmation.
10. Historical failures become permanent regression tests.
11. Ownership, lifetime, locking, execution context and error semantics are documented.
12. Security is reviewed at kernel, parser, filesystem, IPC, DMA and build boundaries.
13. A failed dependency must not be hidden by pretending it is compatible.
14. A source project is not considered ported until its actual build/test path works on RixuriOS.
15. Dependency resolution is recursive and cycle-aware.
16. Dependency versions must be recorded; builds must be reproducible where practical.
17. Source licenses and redistribution obligations must be preserved.
18. Untrusted source/build scripts are never executed with unrestricted system privileges.
19. GUI work cannot consume capacity before the pre-GUI gate.
20. One-owner simplicity is preferred over unnecessary enterprise complexity.

---

# 3. UNIVERSAL ENGINEERING PIPELINE

`SPEC → ABI/DATA MODEL → DESIGN → IMPLEMENT → BUILD → UNIT → NEGATIVE → QEMU → INTEGRATION → HARDWARE → REGRESSION → SECURITY → PERFORMANCE → DOCUMENT → CHECKPOINT`

For software ports, use the additional pipeline:

`GITHUB SOURCE → SOURCE AUDIT → BUILD-SYSTEM DETECTION → DEPENDENCY EXTRACTION → GRAPH SOLVE → PORT PLAN → API TRANSLATION → DEPENDENCY BUILD → PROJECT BUILD → TEST → PACKAGE → INSTALL → ROLLBACK`

Every port records source commit/tag, license, architecture, detected build system, direct dependencies, transitive dependencies, patches, API translations, environment variables, build commands, tests, produced artifacts and unresolved incompatibilities.

---

# FOUNDATION — PHASES 00–09

## PHASE 00 — Governance and Reproducible Build
- Canonical tree and generated-file policy.
- Pinned host/cross toolchains.
- Deterministic compiler/linker/image generation.
- Debug/release profiles and provenance.
- CI, artifact retention and checkpoint ledger.
- ABI/versioning and release-blocker policy.

## PHASE 01 — UEFI Boot and Firmware Handoff
- EFI structures and calling conventions.
- ELF64 kernel loading.
- PT_LOAD allocation/copy/zero-fill.
- ACPI RSDP and GOP discovery.
- Memory map and ExitBootServices retry.
- Boot diagnostics and firmware quirks.

## PHASE 02 — CPU, PMM, VMM and Kernel Memory
- CPUID/MSR and feature policy.
- PMM frame ownership.
- Paging and permission transitions.
- DMA-capable allocation.
- Page-fault handling.
- Real heap allocation/free and reclamation.
- Leak/corruption diagnostics.

**Gate:** kernel memory is genuinely reclaimable.

## PHASE 03 — GDT/TSS/IDT/Exceptions/Interrupts
- GDT/TSS/IST.
- Stable trap-frame ABI.
- Exceptions 0–31.
- IRQ entry/EOI/nesting.
- PIC compatibility and spurious IRQs.
- Nested-fault and malformed-return tests.

## PHASE 04 — ACPI/LAPIC/IOAPIC/Timers/SMP
- ACPI table validation.
- MADT and interrupt overrides.
- LAPIC/x2APIC.
- AP startup and per-CPU state.
- IPI routing.
- APIC timer/HPET/PIT.
- TLB shootdown.

**Gate:** multiple CPUs execute safely.

## PHASE 05 — Synchronization and Kernel Workers
- Spinlocks, IRQ-save locks, mutexes, RW locks, semaphores.
- Wait queues.
- Reference counting.
- Lock ordering/deadlock diagnostics.
- Kernel workers and cancellation.

## PHASE 06 — Processes, Threads and Preemptive Scheduler
- PID/TID lifecycle.
- Process/thread objects.
- Kernel threads.
- User-thread contexts and stacks.
- Timer preemption.
- Per-CPU queues.
- SMP balancing/affinity.
- Scheduler accounting.

## PHASE 07 — Syscall ABI and Uaccess
- Stable syscall numbering/versioning.
- Entry/return ABI.
- Canonical/range checks.
- Fault-safe copyin/copyout.
- FD/object validation.
- errno/restart policy.
- ABI fuzzing.

## PHASE 08 — User VM and ELF64 Execution
- Independent address spaces.
- Kernel/user permissions.
- ELF validation and PT_LOAD.
- BSS and aligned user stack.
- argc/argv/envp/auxv.
- PIE/non-PIE.
- exec replacement and teardown.
- User page-fault isolation.

**Gate:** genuine ring-3 programs run independently.

## PHASE 09 — IPC and Process Communication
- Pipes/FIFOs.
- Events/wait objects.
- Shared memory.
- Process groups.
- Signals architecture.
- Unix-domain sockets and FD passing.
- Process-death cleanup.

---

# HARDWARE CORE — PHASES 10–21

## PHASE 10 — PCIe, MCFG, MMIO and DMA
- PCI/ECAM discovery.
- Capability parsing.
- BAR/MMIO ownership.
- DMA mapping/cache rules.
- MSI/MSI-X.
- Device/driver lifecycle.
- Bridge/multifunction handling.
- IOMMU abstraction.

## PHASE 11 — Storage Core
- Block-device registry.
- BIO/request/SG.
- Queue ordering.
- Flush/FUA/barriers.
- Timeout/retry/reset.
- Cache/writeback.
- Storage error states.

## PHASE 12 — NVMe
- Controller reset/enable.
- Admin and I/O queues.
- Identify.
- PRP/SGL.
- Polling and interrupts.
- Read/write/flush.
- Timeout/reset recovery.
- Physical-device evidence.

## PHASE 13 — VFS and RixFS
- Vnode/inode/dentry/superblock/file model.
- Mount/path resolution.
- Directory/file operations.
- Permissions.
- Links and safe traversal.
- RixFS on-disk format.
- Journal/checksum/orphan recovery.
- fsck/emergency mount.

## PHASE 14 — Time/RTC/Desktop ACPI
- Monotonic/realtime clocks.
- RTC/CMOS.
- Timers/sleep.
- Desktop ACPI.
- Reboot/poweroff/reset.
- Idle/thermal safety.

## PHASE 15 — USB/xHCI
- xHCI register model.
- Rings/TRBs/cycles.
- Device contexts.
- Port reset/address/configure.
- Control/bulk/interrupt transfers.
- MSI/MSI-X/DMA.
- Timeout/reset/hotplug recovery.

## PHASE 16 — USB HID/Input
- HID descriptors/reports.
- Keyboard/mouse.
- Interrupt-IN.
- Repeat/rollover.
- Input event ABI.
- Hotplug cleanup.
- Physical HID evidence.

## PHASE 17 — TTY/PTY/Console
- TTY/PTY.
- Canonical/raw modes.
- UTF-8/ANSI/VT.
- Queues/flow control.
- Controlling terminal.
- Parser fuzzing.

## PHASE 18 — Shell and Job Control
- Parser/quoting/escaping.
- Expansion/globbing.
- Pipelines/redirections.
- Environment.
- Jobs/process groups.
- Signals.
- Builtins/substitution.

## PHASE 19 — Base Unix Userland
- Filesystem utilities.
- Text utilities.
- Process tools.
- Storage tools.
- Hardware/network diagnostics.
- Correct exit/error behavior.

## PHASE 20 — Single-User Credentials and Security
- Primary local user.
- UID/GID primitives.
- Privileged kernel boundary.
- File permissions.
- Session ownership.
- Audit/security events.
- W^X/ASLR/stack-hardening integration.

## PHASE 21 — Network Stack and Real NICs
- Ethernet/ARP/IPv4/ICMP/UDP.
- TCP ordering/ACK/retransmission/window.
- Socket blocking/nonblocking.
- Routing.
- DNS.
- E1000/QEMU.
- RTL8125 physical path and recovery.

---

# USERSPACE ABI — PHASES 22–35

## PHASE 22 — Native libc Surface
- Headers/types.
- errno.
- strings/memory/stdio.
- allocation.
- filesystem/process/time wrappers.
- sockets/signals.
- compatibility matrix.

## PHASE 23 — Dynamic ELF/Shared Libraries/TLS
- PT_INTERP/PT_DYNAMIC.
- DT_NEEDED/SONAME.
- REL/RELA.
- GOT/PLT.
- symbol lookup.
- dlopen/dlsym/dlclose.
- PT_TLS and `%fs`.
- TLS models.

**Gate:** real dynamically linked programs work.

## PHASE 24 — mmap and User VA Manager
- mmap/munmap/mprotect/msync.
- Anonymous/file mappings.
- Mapping allocator.
- Guard regions.
- COW/reference accounting.
- OOM behavior.

## PHASE 25 — Threads/Futex/pthreads
- Thread creation.
- User stacks.
- TLS lifecycle.
- Futex wait/wake/timeout.
- Mutex/condition variables.
- Join/detach.
- Abnormal cleanup.

## PHASE 26 — Signals
- Dispositions/masks.
- Pending signals.
- User delivery.
- Signal frame restoration.
- Thread targeting.
- Interrupted syscalls.
- Job-control signals.

## PHASE 27 — Init and Process Lifecycle
- Reliable spawn/exec/wait/exit.
- FD inheritance/close-on-exec.
- Orphan/zombie handling.
- PID 1.
- Single-user boot target.
- Crash containment.

## PHASE 28 — Low-Level Display Preparation
- Framebuffer ownership.
- Pixel formats/stride.
- Display enumeration.
- EDID where practical.
- Display diagnostics.

**No GUI.**

## PHASE 29 — Generic GPU Foundation
- GPU device model.
- Memory objects.
- Command submission.
- Fences.
- Reset/hang recovery.
- GPU security boundaries.

## PHASE 30 — AMD GPU Target Preparation
- AMD PCI/BAR handling.
- Required MMIO.
- Firmware-loading architecture.
- Display-engine groundwork.
- Physical target evidence.

## PHASE 31 — USB Storage
- Mass-storage transport.
- Removable block devices.
- Media insertion/removal.
- Recovery after removal.

## PHASE 32 — GPT/Partitioning
- GPT CRC validation.
- Protective MBR.
- Sector-size handling.
- Stable partition identity.
- Safe destructive operations.

## PHASE 33 — Filesystem Power-Loss Recovery
- Journal replay.
- Metadata checksums.
- Orphans.
- Interrupted writes.
- fsck safety.
- Real disposable-media power-loss tests.

## PHASE 34 — Root Filesystem/System Layout
- `/bin`, `/lib`, `/etc`, `/dev`, `/home`, `/usr`, `/var`, `/tmp` policy.
- Device nodes.
- Logs/crash dumps.
- Recovery shell.

## PHASE 35 — FIRST USABLE OS
A supported physical desktop must:
- boot the real root filesystem;
- reach a real shell;
- execute dynamic programs;
- create/read/write/remove files;
- run multiple processes;
- use physical keyboard/input;
- configure a physical NIC;
- reboot/poweroff;
- enter recovery mode.

**Milestone:** usable terminal-first single-user OS. Not release quality. Not GUI.

---

# SYSTEM MATURITY — PHASES 36–59

## PHASE 36 — `/dev` and Device Lifecycle
Stable device nodes, hotplug and teardown.

## PHASE 37 — Diagnostic System Interfaces
Process, CPU, memory, device, filesystem and network state views; intentionally not a Linux ABI clone.

## PHASE 38 — ABI Completeness/Frozen Core ABI
Inventory syscalls, remove required ENOSYS gaps, validate structures and freeze the core ABI candidate.

## PHASE 39 — Fault Containment
Uniform errors, process crash reporting, timeout states and recovery reporting.

## PHASE 40 — Structured Logging/Tracing
Kernel logs, boot timeline, syscall/scheduler/device/storage/network traces and crash metadata.

## PHASE 41 — Configuration System
Atomic configuration, validation, safe defaults and recovery from malformed configuration.

## PHASE 42 — Minimal Service Manager
Single-user service definitions, dependencies, restart, timeout, logging and shutdown ordering.

## PHASE 43 — Boot Targets
Normal, diagnostic, recovery and clean-shutdown targets.

## PHASE 44 — Local Login/Session
One primary owner session, authentication boundary, environment and TTY ownership.

## PHASE 45 — Privilege/File Security
Permission correctness, privileged operations, temporary files and TOCTOU/symlink review.

## PHASE 46 — Memory Hardening
SMEP/SMAP, NX/W^X, stack protection, heap hardening and page-table audits.

## PHASE 47 — DMA/IOMMU Hardening
Device isolation, DMA lifetime, invalid-DMA diagnostics and reset cleanup.

## PHASE 48 — Network Reliability
TCP retransmission/window/MTU, DNS retry/cache, DHCP renewal, link recovery and NIC reset.

## PHASE 49 — Network Utilities
Interface/route/DNS tools, ping, diagnostic clients and understandable failure reporting.

## PHASE 50 — Time Synchronization
RTC initialization, network time, clock adjustment and consistent timestamps.

## PHASE 51 — Storage Reliability/Performance
Queue tuning, cache metrics, writeback, flush measurement and large-file stress.

## PHASE 52 — Scheduler Performance
Context-switch/wakeup latency, SMP scaling, priority inversion and starvation testing.

## PHASE 53 — Memory Pressure/OOM
Global/process accounting, reclaim, OOM policy and mapping exhaustion.

## PHASE 54 — Single-User Resource Limits
FD/process/thread/memory/address-space/IPC limits and disk-space warnings.

## PHASE 55 — FD/Object Lifetime Audit
close-on-exec, duplication, reference counts, concurrent close/read/write and process death cleanup.

## PHASE 56 — Concurrency/Race Audit
SMP, interrupt, device-removal, filesystem, signal/thread and FD race corpus.

## PHASE 57 — Recovery Toolkit
Offline-capable repair shell, logs, disk/network diagnostics, configuration rollback and safe reboot.

## PHASE 58 — Atomic Update/Rollback
Versioned system updates, previous bootable version, failed-update detection and rollback.

## PHASE 59 — DAILY TERMINAL OS BASELINE
Stable enough that the owner can use RixuriOS as the primary terminal operating environment on supported hardware.

---

# RECURSIVE SOURCE-TO-NATIVE SOFTWARE SYSTEM — PHASES 60–79

This section is the major software-ecosystem objective of RixuriOS.

## PHASE 60 — Source Fetcher
- GitHub repository URL handling.
- Git clone/fetch.
- Commit/tag/branch pinning.
- Source integrity metadata.
- Offline source cache.
- License metadata.

## PHASE 61 — Project Detector
Automatically identify common project/build forms:
- Make/autoconf/automake.
- CMake.
- Meson/Ninja.
- Cargo.
- Python packaging.
- shell/configure projects.
- custom build scripts.

Unknown projects fall into an explicit manual-port state rather than a fake success state.

## PHASE 62 — Dependency Extractor
- Parse declared dependencies.
- Detect compiler/linker requirements.
- Detect library/header requirements.
- Detect generated-code tools.
- Detect runtime dependencies.
- Record version constraints.
- Distinguish build-time and runtime dependencies.

## PHASE 63 — Recursive Dependency Graph Engine
- Build a complete directed dependency graph.
- Resolve transitive dependencies recursively.
- Detect cycles.
- Detect conflicting versions.
- Topologically order builds.
- Cache previously solved graphs.
- Produce a human-readable plan before changes are made.

Example:

`app → libA → libC → libF`

must resolve `libF` before `libC`, then `libA`, then `app`.

## PHASE 64 — Source/API Compatibility Scanner
- Scan source for unsupported POSIX/Linux assumptions.
- Detect syscalls and libc APIs.
- Detect headers and compiler features.
- Detect filesystem paths and environment assumptions.
- Detect Linux-specific APIs.
- Classify each incompatibility as native, compatible, patchable or unsupported.

## PHASE 65 — RixuriOS API Mapping Engine
- Map standard APIs to RixuriOS libc/syscalls.
- Maintain versioned API translation rules.
- Generate compile-time compatibility definitions where safe.
- Provide documented port shims where necessary.
- Never silently change semantics.

## PHASE 66 — Automatic Port Patch System
- Generate minimal source patches for known incompatibilities.
- Store patches separately from upstream source.
- Make patches deterministic and reviewable.
- Rebase/reapply patches against newer upstream commits.
- Reject ambiguous patches instead of guessing.

## PHASE 67 — Dependency Port Worker
For every dependency node:
1. fetch source;
2. inspect license/build system;
3. resolve its dependencies;
4. recursively port those dependencies;
5. apply RixuriOS API adaptations;
6. build;
7. test;
8. package;
9. expose the result to its parent.

## PHASE 68 — Sandboxed Port Builds
- Build userspace software without unrestricted privileges.
- Controlled filesystem/network access.
- Resource limits.
- Deterministic environment.
- Build logs.
- Failure capture.
- Reproducibility metadata.

## PHASE 69 — Build-System Translation Layer
- Native wrappers for C/C++ build systems.
- Cargo target/toolchain integration.
- Python build backend support where useful.
- Configure environment translation.
- Compiler/linker flag translation.
- Install-prefix translation.

## PHASE 70 — RixuriOS Port Database
Store reusable knowledge for:
- repository/source identity;
- known dependencies;
- known incompatibilities;
- API mappings;
- patches;
- successful toolchains;
- test commands;
- known failures.

The database accelerates future ports without pretending upstream software is permanently forked.

## PHASE 71 — Port Cache and Artifact Cache
- Cache source revisions.
- Cache solved dependency graphs.
- Cache successful builds.
- Cache failed port attempts with reasons.
- Invalidate caches on ABI/toolchain changes.

## PHASE 72 — Native Package Builder
- Convert successful builds into RixuriOS packages.
- Metadata.
- Version/source identity.
- Dependency metadata.
- File manifest.
- ABI requirements.
- License information.
- Integrity hashes.

## PHASE 73 — Transactional Package Installer
- Install dependency closure.
- Atomic file changes where possible.
- Ownership tracking.
- Conflict detection.
- Uninstall.
- Upgrade/downgrade.
- Rollback.

## PHASE 74 — `rix install <GitHub repository>`
Provide the owner-facing workflow:

`rix install <github-url>`

The command must:
- fetch the repository;
- inspect it;
- display the dependency graph;
- resolve dependencies recursively;
- port/build dependencies;
- port/build the requested project;
- run tests;
- create/install packages;
- report exactly what changed.

No pre-created package entry is required for a project if the source-to-native pipeline can successfully port it.

## PHASE 75 — Dependency Failure and Human-Assisted Porting
- Clear failure reasons.
- Show the exact incompatible API/build step.
- Suggest an explicit port action.
- Allow owner-approved patches.
- Resume from the failed dependency.
- Never restart the whole graph unnecessarily.

## PHASE 76 — Multi-Language Porting
Expand the same model beyond C/C++ to supported ecosystems such as Rust and selected scripting/runtime projects, provided the required runtime can itself be built natively.

## PHASE 77 — Runtime Dependency Resolution
Distinguish:
- build dependency;
- link dependency;
- runtime shared library;
- optional dependency;
- test-only dependency.

Install only what the final application actually requires, while retaining reproducible dependency metadata.

## PHASE 78 — Port Security and Supply-Chain Verification
- Source commit verification.
- Signature/hash support where available.
- License checks.
- Patch provenance.
- Build sandboxing.
- Dependency confusion protection.
- No privileged build scripts by default.

## PHASE 79 — RECURSIVE GITHUB-TO-RIXURIOS MILESTONE

**Milestone:** the owner can give RixuriOS a suitable GitHub project and the system can automatically resolve, recursively port, build, test, package and install its dependency closure when all required APIs/build environments are supported.

This is **not** a static package repository milestone. It is a source-to-native software platform milestone.

---

# PRE-GUI PRODUCTIZATION — PHASES 80–99

## PHASE 80 — Native Development Toolchain
Compiler/assembler/debugger/object/ELF tools sufficient for developing directly on RixuriOS.

## PHASE 81 — Native Build Environment
Make/build tools, headers, libraries, pkg-config-like metadata and development packages.

## PHASE 82 — POSIX Compatibility Expansion
Implement only compatibility that materially increases the number of useful real-world source projects that can be ported.

## PHASE 83 — Real musl Integration
Build and validate musl against the RixuriOS syscall ABI, including TLS, pthreads and dynamic linking.

## PHASE 84 — Native Shell/Scripting Maturity
Reliable scripts, functions, quoting, signals, job control and administration.

## PHASE 85 — Native `rix` Administration Tool
Status, diagnostics, hardware, storage, network, service, package, update and recovery commands.

## PHASE 86 — Offline Documentation
System, syscall, filesystem, driver, recovery, porting and package documentation available locally.

## PHASE 87 — One-Command Diagnostics
Produce a complete owner-readable diagnostic bundle covering boot, CPU, memory, storage, USB, network, services, packages and recent crashes.

## PHASE 88 — Backup/Restore
Home/configuration backup, verification, restore and recovery from removable media.

## PHASE 89 — User/Data Safety
Atomic writes, disk-full handling, read-only behavior, safe temporary files and destructive-operation warnings.

## PHASE 90 — Desktop Hardware Expansion
Qualify additional AMD/Intel desktop CPUs, NVMe, NIC, USB and GPU targets based on actual value to the single owner.

## PHASE 91 — Security Hardening
Final SMEP/SMAP/W^X/ASLR/stack/heap/uaccess/DMA/ELF/network/USB audits.

## PHASE 92 — Long-Run Soak
Multi-day workloads, process churn, filesystem operations, network sessions, NVMe workloads, USB hotplug and memory pressure.

## PHASE 93 — Full Regression Freeze
Historical failures, ABI, filesystem, driver, package-porting and update regressions run on every release candidate.

## PHASE 94 — Physical Hardware Acceptance
Cold boot, warm reboot, poweroff, storage, filesystem, USB/HID, networking, SMP, memory pressure and recovery PASS evidence per supported machine.

## PHASE 95 — Release Candidate 1
Reproducible image, installer, native software pipeline, terminal, storage, network, recovery and security PASS.

## PHASE 96 — Release Candidate 2
Bug-fix only. Repeat physical hardware, power-loss, update/rollback and recursive software-porting regression tests.

## PHASE 97 — Release Candidate 3
Final stability pass; no known critical kernel crash, filesystem corruption path or unrecoverable update path.

## PHASE 98 — RixuriOS 1.0 Preparation
Versioning, release notes, hardware matrix, installation media, recovery path, signatures/checksums and known limitations.

## PHASE 99 — FINAL PRE-GUI BASELINE

Required:
- stable boot;
- stable memory management;
- SMP/preemption;
- user VM;
- dynamic ELF/TLS;
- threads/futex/signals;
- storage/filesystem recovery;
- USB/HID;
- network/DNS/TCP recovery;
- libc/musl;
- shell/utilities;
- recursive GitHub source-to-native software installation;
- installer/update/rollback;
- recovery shell;
- security hardening;
- physical hardware evidence;
- regression and soak testing.

**Gate:** RixuriOS is already a complete, usable terminal-first single-user OS before GUI development starts.

---

# PHASE 100 — GRAPHICAL DESKTOP / GUI

This is the **only** phase where the actual graphical product is built.

### 100.1 Display server/compositor
- Display ownership.
- Outputs/monitors.
- Framebuffer/scanout.
- Rendering synchronization.
- Cursor.
- GPU acceleration where genuinely supported.
- Display/GPU crash recovery.

### 100.2 Window system
- Windows/surfaces.
- Focus/input routing.
- Clipboard.
- Window lifecycle.
- Drag/drop where useful.

### 100.3 Desktop shell
- Desktop session.
- Application launcher.
- Task/window management.
- Notifications.
- Terminal application.
- File manager.
- Settings.

### 100.4 Single-owner desktop UX
- Local login/automatic-login option.
- Owner session recovery.
- GUI crash recovery to terminal.
- No unnecessary multi-seat infrastructure.
- Recovery remains usable without GUI.

### 100.5 GUI security/performance
- Application isolation.
- Clipboard/input boundaries.
- Privileged-operation confirmation.
- GPU command security.
- Frame pacing/input latency.
- Memory/CPU/GPU measurement.

### 100.6 Final desktop acceptance
- Physical boot into GUI.
- Keyboard/mouse.
- Terminal application.
- File management.
- Networking.
- Settings.
- Reboot/poweroff.
- GUI crash recovery.
- Update/reboot/recovery cycle.

**FINAL PRODUCT GATE:** Phase 100 passes only after all required pre-GUI gates have PASS evidence.

---

# DEFINITION OF DONE

A phase is COMPLETE only when applicable:
- specification exists;
- real implementation exists;
- clean build passes;
- unit/negative/boundary tests pass;
- QEMU evidence exists where relevant;
- physical evidence exists where relevant;
- historical regressions are covered;
- security review is complete;
- performance is measured where relevant;
- failure/recovery is documented;
- limitations are explicit;
- checkpoint evidence is archived.

For a software port, COMPLETE additionally requires:
- pinned source revision;
- license recorded;
- dependency closure resolved;
- all required dependencies built natively;
- RixuriOS API compatibility verified;
- patches recorded;
- package manifest generated;
- install/uninstall tested;
- runtime dependencies verified;
- no hidden host-library dependency;
- reproducible build information recorded.

`SKIP`, `ENOSYS`, `NOT TESTED`, `DEGRADED`, `BLOCKED`, `FAIL` and `UNSUPPORTED` never mean COMPLETE.

---

# RIXURIOS MILESTONES

| Milestone | Phase | Meaning |
|---|---:|---|
| Kernel foundation | 00–09 | Real kernel/process/VM/ABI foundation |
| Hardware foundation | 10–21 | Storage/USB/TTY/network foundations |
| Userspace foundation | 22–27 | Dynamic userspace, VM, threads and signals |
| First usable OS | **35** | Real physical terminal OS |
| Daily terminal OS | **59** | Primary terminal environment for the owner |
| Recursive software platform | **79** | GitHub → dependency graph → recursive port/build/install |
| Pre-GUI 1.0 | **99** | Complete non-graphical OS |
| Graphical RixuriOS | **100** | Full desktop product |

## Core long-term loop

```text
GitHub repository
      ↓
source revision + license
      ↓
project/build-system detection
      ↓
direct dependency extraction
      ↓
recursive dependency graph
      ↓
for each dependency:
    fetch source
    analyze compatibility
    resolve its dependencies
    port to RixuriOS APIs
    build
    test
    package
      ↓
build requested project
      ↓
run tests
      ↓
package dependency closure + application
      ↓
transactional install
      ↓
record port knowledge for future builds
```

The goal is not to make RixuriOS a giant manually curated package list. The goal is to make **RixuriOS itself capable of turning suitable open-source source trees into native RixuriOS software recursively and safely**.

GUI remains Phase 100 because the operating system should already be useful, recoverable and capable of acquiring real software before a graphical desktop is added.
