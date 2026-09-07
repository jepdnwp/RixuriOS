# RixuriOS Master Development Roadmap — v4

**Architecture:** x86_64 / AMD64, 64-bit only  
**Kernel:** freestanding C11/C17 + minimal x86_64 assembly  
**Userspace:** Unix-like, musl/POSIX-oriented, dynamically linked  
**Product principle:** terminal-first, hardware-real, recovery-first  
**GUI:** Phase 28 and absolutely last

> This is an engineering roadmap, not a feature wishlist. Every item has an implementation path, dependencies, tests, failure handling and evidence gates. Compilation never closes a phase.

## 0. Non-negotiable engineering rules

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

## 1. Universal phase workflow

Every feature follows:

`SPEC → ABI/DATA MODEL → DESIGN → IMPLEMENT → BUILD → UNIT → NEGATIVE → QEMU → INTEGRATION → HARDWARE → REGRESSION → SECURITY → PERFORMANCE → DOCUMENT → CHECKPOINT`

For every implementation task the coding agent must record:

- files created/changed;
- public APIs and ABI changes;
- ownership/lifetime rules;
- locking/context rules;
- error codes and recovery behavior;
- hardware assumptions;
- test commands and expected observations;
- evidence artifact location;
- unresolved limitations.

## 2. Checkpoint vocabulary

Each phase uses granular IDs such as `P07-NVME-12` rather than one vague phase status.

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
- symbol/map generation.
- deterministic disk/ESP image creation.

### Engineering infrastructure
- coding standards;
- ABI change policy;
- versioning policy;
- changelog;
- architecture decision records;
- test-result schema;
- checkpoint ledger;
- crash-log format;
- hardware inventory format.

### Checkpoints
`P00-01` clean build → `P00-02` deterministic image → `P00-03` CI → `P00-04` artifact retention → `P00-05` documentation baseline.

---

# PHASE 01 — UEFI Boot and Firmware Handoff

### Implementation
- Correct EFI table/function-pointer layouts.
- Loaded-image/filesystem access.
- ELF64 validation with overflow checks.
- PT_LOAD allocation/copy/zero-fill.
- kernel entry contract.
- ACPI RSDP discovery.
- GOP discovery.
- final UEFI memory map.
- `ExitBootServices()` retry protocol.
- boot handoff versioning.

### Debugging
If firmware calls fail, inspect ABI, calling convention, structure packing, stack alignment, function pointers, CR3/page tables and memory corruption before changing random offsets.

### Checkpoints
`P01-01` ELF validation, `P01-02` real UEFI boot, `P01-03` memory map, `P01-04` EBS retry, `P01-05` ACPI/GOP, `P01-06` historical UEFI #UD regression.

---

# PHASE 02 — CPU Bring-up and Memory Safety

### CPU
- CPUID feature inventory.
- MSR access wrappers.
- control-register policy.
- NX/WP/SMEP/SMAP policy.
- syscall CPU feature policy.
- invariant TSC detection.

### PMM
- UEFI descriptor parser.
- reserved ranges.
- frame allocation/free.
- DMA zones/alignment.
- reference/ownership model.

### VMM
- 4/5-level paging according to CPU capability.
- kernel address space.
- user address spaces.
- map/unmap/protect.
- page faults.
- TLB invalidation and shootdown design.
- huge pages where justified.

### Heap
- early allocator;
- size classes/slabs;
- alignment;
- overflow checks;
- guard/debug mode;
- leak diagnostics.

### Checkpoints
`P02-01` PMM → `P02-02` page tables → `P02-03` permissions → `P02-04` page faults → `P02-05` heap → `P02-06` SMP TLB design → `P02-07` security review.

---

# PHASE 03 — GDT, TSS, IDT, Exceptions and Interrupt Framework

- GDT kernel/user segments.
- TSS and `ltr`.
- IST stacks.
- complete trap-frame ABI.
- exceptions 0–31.
- page-fault diagnostics.
- IRQ stubs.
- interrupt nesting policy.
- interrupt-safe logging.
- EOI policy.
- PIC compatibility/disable.

### Checkpoints
`P03-01` exception entry → `P03-02` IST → `P03-03` IRQ entry/return → `P03-04` fault decoding → `P03-05` nested interrupt tests.

---

# PHASE 04 — ACPI, LAPIC, IOAPIC, Timers and SMP

### ACPI
- RSDP checksum.
- XSDT/RSDT parsing.
- MADT CPU/LAPIC/IOAPIC entries.
- interrupt-source overrides.
- FADT/HPET/MCFG discovery architecture.

### Interrupt routing
- correct ACPI polarity/trigger translation.
- IOAPIC redirection entries.
- LAPIC/x2APIC where supported.
- MSI/MSI-X groundwork.

### Time
- APIC timer.
- HPET fallback where useful.
- PIT compatibility.
- monotonic clock.
- wall-clock source architecture.
- timer wheel/high-resolution timers.

### SMP
- AP trampoline/startup.
- per-CPU structures.
- CPU online/offline state.
- barriers and cache coherency assumptions.
- inter-processor interrupts.
- TLB shootdowns.

### Checkpoints
`P04-01` MADT → `P04-02` IOAPIC → `P04-03` timer IRQ → `P04-04` AP startup → `P04-05` cross-CPU IPI → `P04-06` synchronization regression.

---

# PHASE 05 — Kernel Synchronization, Wait Queues and Workqueues

- spinlocks;
- irq-save locks;
- mutexes;
- rwlocks;
- semaphores;
- condition/wait queues;
- atomic reference counting;
- lock ordering rules;
- deadlock diagnostics;
- deferred interrupt work;
- kernel worker threads;
- cancellation semantics.

### Gate
Every lock documents whether it is legal in interrupt, process and sleepable context.

---

# PHASE 06 — Process, Thread and Scheduler Core

### Process objects
- PID allocation/reuse protection.
- parent/child relationships.
- credentials.
- address-space ownership.
- file descriptor table.
- signal state.
- process groups/sessions.
- exit state/zombies.

### Threads
- kernel threads.
- user threads.
- saved CPU context.
- kernel stack.
- TLS/thread pointer architecture.

### Scheduler
- preemption.
- per-CPU runqueues.
- priorities/fairness.
- sleep/wakeup.
- timer expiration.
- idle threads.
- SMP load balancing.
- CPU affinity.
- starvation diagnostics.

### Checkpoints
`P06-01` context switch → `P06-02` timer preemption → `P06-03` sleep/wakeup → `P06-04` multi-CPU scheduling → `P06-05` process lifecycle.

---

# PHASE 07 — Syscall ABI and User/Kernel Boundary

- stable syscall numbering/version policy;
- syscall entry/return;
- kernel stack transition;
- user pointer validation;
- copy-in/copy-out;
- canonical-address validation;
- FD validation;
- errno/error mapping;
- restartable syscalls;
- syscall tracing.

### Initial ABI
`read`, `write`, `openat`, `close`, `stat`, `getpid`, `exit`, `wait`, `mmap`, `munmap`, `mprotect`, `ioctl`, `poll`, `nanosleep`, process creation/exec and signal primitives.

### Gate
A real ring-3 process enters kernel mode, accesses only authorized memory, performs real I/O and returns a defined result.

---

# PHASE 08 — User Address Spaces and ELF64 Execution

- independent page tables.
- user/kernel split.
- stack allocation/guard page.
- ELF header/program-header validation.
- PT_LOAD mapping.
- BSS zeroing.
- PIE/non-PIE policy.
- ASLR architecture.
- `argc/argv/envp/auxv`.
- stack alignment.
- executable W^X policy.
- `exec` replacement.

### Checkpoints
`P08-01` static ELF → `P08-02` malformed ELF rejection → `P08-03` ring-3 start → `P08-04` exec → `P08-05` user memory fault isolation.

---

# PHASE 09 — IPC, Pipes, Signals, Events and Shared Memory

Add the missing Unix process machinery before building a sophisticated shell:

- anonymous pipes;
- named pipes/FIFOs;
- signals and signal masks;
- signal delivery/return frames;
- process groups;
- event objects;
- poll/select-like waiting;
- shared memory with explicit permissions;
- Unix-domain socket architecture;
- SCM-like descriptor passing architecture;
- futex-like userspace synchronization primitive if justified.

### Gate
Two real processes communicate without bypassing kernel authorization or lifetime rules.

---

# PHASE 10 — PCIe, ACPI MCFG, MMIO, DMA and Device Model

- PCI configuration access.
- PCIe extended configuration.
- MCFG/ECAM.
- capability traversal.
- BAR sizing/mapping.
- bus mastering.
- DMA allocation/mapping/unmapping.
- cache coherency.
- IOMMU/VT-d/AMD IOMMU architecture.
- MSI/MSI-X.
- driver registration/matching.
- resource ownership.
- probe/remove/reset.
- hotplug state machine.
- device dependency graph.

### Required identities
- RTL8125 `10EC:8125`.
- RX 6800 XT `1002:73BF`.
- QEMU GPU `1B36:0100`.
- target NVMe `1CC1:5370` where applicable.

---

# PHASE 11 — Storage Core

### Block layer
- block device registry;
- sector/block geometry;
- BIO/request objects;
- scatter/gather;
- queue depth;
- barriers;
- flush/FUA semantics;
- completion callbacks;
- timeout/cancellation;
- retry policy;
- error propagation.

### Cache
- page/buffer cache;
- dirty tracking;
- writeback;
- eviction;
- coherency with direct I/O.

### Checkpoints
`P11-01` block API → `P11-02` disposable image → `P11-03` read/write → `P11-04` flush → `P11-05` timeout/recovery.

---

# PHASE 12 — Real NVMe Driver

### Controller
- reset/disable/enable state machine;
- CAP/VS/CC/CSTS validation;
- admin queue creation;
- Identify Controller;
- Identify Namespace;
- namespace lifecycle.

### I/O
- submission/completion queues;
- phase tags;
- PRP list construction;
- SGL where needed;
- DMA constraints;
- interrupt/poll completion;
- read/write/flush;
- timeout and controller reset recovery.

### Mandatory evidence ladder
`P12-01 PCI → P12-02 BAR → P12-03 RDY → P12-04 Identify Controller → P12-05 Identify Namespace → P12-06 namespace online → P12-07 real read → P12-08 real write → P12-09 real flush → P12-10 timeout/recovery → P12-11 physical hardware regression`.

No controller-detected message may substitute for I/O evidence.

---

# PHASE 13 — VFS and RixFS

### VFS
- vnode/inode abstraction;
- dentry/path cache;
- mount tree/namespaces;
- superblocks;
- file objects;
- FD tables;
- path normalization;
- symlink handling;
- directory iteration;
- locks;
- stat family;
- rename/unlink/mkdir/rmdir;
- permissions hooks.

### RixFS
- versioned on-disk specification;
- superblock;
- inode format;
- extents/direct data;
- directories;
- allocation bitmap/metadata;
- free-space manager;
- journal;
- checksums;
- orphan/recovery handling;
- mount/unmount;
- fsck;
- truncate/read/write.

### Critical safety
Unknown, missing, corrupt or incompatible media must return an error/recovery option. **Never silently format.**

---

# PHASE 14 — Time, RTC, Power and Hardware Management

- RTC/CMOS abstraction where available.
- monotonic/realtime clocks.
- timezone database architecture.
- sleep/timer APIs.
- ACPI power states.
- reboot/shutdown.
- CPU idle states.
- thermal sensors architecture.
- fan/power-management hooks.
- battery/AC adapter model where hardware provides it.
- suspend/resume architecture.

---

# PHASE 15 — USB/xHCI Core

- xHCI capability/operational/runtime registers.
- DCBAA.
- scratchpads.
- command ring.
- transfer rings.
- event ring/ERST.
- TRB cycle ownership.
- slots.
- device/input contexts.
- port reset.
- Address Device.
- Configure Endpoint.
- control/bulk/interrupt transfers.
- interrupters/MSI/MSI-X.
- DMA/cache ordering.
- timeout/reset/recovery.
- hotplug.

### Regression
Dedicated instrumentation for historical Address Device completion code **11**. Preserve every TRB, slot, context pointer, route string, port state and completion code needed to diagnose it.

---

# PHASE 16 — USB HID, Keyboard, Mouse and Input

- USB descriptor parsing.
- HID descriptor.
- report descriptor parser.
- boot protocol.
- report protocol.
- interrupt-IN transfers.
- keycode/modifier state.
- press/release/repeat.
- rollover handling.
- mouse buttons/motion/wheel.
- hotplug/unplug.

### Regression
Real keyboard input must travel through xHCI → USB → HID → input subsystem → TTY. Historical `0x74` (`t`) evidence remains a regression target; synthetic key injection cannot close the hardware gate.

---

# PHASE 17 — TTY, PTY, Console and Terminal Engine

- TTY objects.
- PTY master/slave.
- canonical/raw modes.
- termios-like configuration.
- echo.
- input/output queues.
- UTF-8.
- ANSI/VT parser.
- terminal dimensions.
- controlling terminal.
- sessions/process groups.
- foreground ownership.
- terminal signals.
- pipes and redirection integration.
- console recovery path.

---

# PHASE 18 — Rixuri Shell 2.0

### Language
- lexer/parser/AST;
- quoting/escaping;
- variables/environment;
- parameter expansion;
- command substitution;
- arithmetic expansion;
- pathname expansion;
- comments;
- here-documents;
- operators.

### Execution
- PATH search;
- builtins;
- external exec;
- pipelines;
- redirections;
- `&&`, `||`, `;`;
- subshells;
- background jobs;
- process groups;
- signals;
- exit status;
- `exec`.

### Interactive
- history;
- persistent history;
- line editor;
- cursor movement;
- completion;
- aliases/functions;
- startup scripts;
- prompt expansion;
- job control.

### Gate
Real programs must compose through real process/pipe/file APIs; demos that merely print expected text do not count.

### Current evidence — 2026-09-06

The disposable NVMe-backed RixFS image was exercised through the real serial-to-TTY shell path after a strict build. The observed commands included `echo one > /tmp`, `echo two >> /tmp`, `cat /tmp`, a three-stage `echo | grep | grep` pipeline, a background `true &`, and a foreground command following the background launch. The console produced the expected `one`, `two`, `alpha`, and `foreground` results, with `NVMe: controllers=1`, `VFS: mount nvme0n1 rc=0`, and `RIXURI:KERNEL_READY` in the same run.

This closes the observed execution cases for append redirection, pipeline depth greater than two, and background launch. It does not close the broader Phase 18 gate: background completion notification, explicit `waitpid(WNOHANG)` output, foreground process-group signal behavior, subshells, here-documents, malformed-pointer runtime tests, and physical USB keyboard input remain open.

---

# PHASE 19 — Unix Coreutils and System Utilities

Implement real programs over RixuriOS APIs:

`ls cp mv rm mkdir rmdir touch ln stat cat head tail wc cut tr sort uniq grep sed find xargs env printf echo pwd cd kill ps sleep uname mount umount df du free dmesg true false test which`

Then add:

- tar/archive tools;
- checksum tools;
- text processing;
- process inspection;
- disk utilities;
- network utilities;
- diagnostic tools.

Every command gets argument validation, errors, exit status and pipeline behavior.

### Current evidence — 2026-09-06

`/bin/touch` is implemented over the existing `openat(O_WRONLY|O_CREAT, 0644)` and `close` ABI. A real QEMU serial harness passed new-file creation, reopening an existing file, missing-parent rejection, cleanup with `/bin/rm`, and final directory verification. Timestamp mutation semantics remain unsupported because the current stat/inode ABI exposes no timestamp update operation. The existing kernel `RIX_SYS_STAT` path is now exposed through libc and `/bin/stat`; QEMU passed regular-file, directory, and missing-path observations. Regular-file hard links are now implemented as well: QEMU passed same-inode source/alias observations, alias survival after source unlink, and invalid-source failures. Directory links and symbolic links remain unsupported. Default `/bin/head` and `/bin/tail` now support regular-file and stdin/pipeline modes; QEMU passed both through `/usr/bin/args` pipelines and missing-path failures. Options, multi-file labels, and large-file tail behavior remain open. The text-core group `/usr/bin/wc`, `/usr/bin/cut`, `/usr/bin/tr`, `/usr/bin/sort`, and `/usr/bin/uniq` is implemented with bounded stdin/path behavior and QEMU-validated pipeline/error scenarios; full POSIX option, locale, and unbounded-input semantics remain open. Environment/shell utilities `/usr/bin/env`, `/usr/bin/printf`, `/bin/pwd`, and `/usr/bin/which` are also QEMU-validated for the current fixed environment and PATH model; full environment mutation, dynamic cwd, and POSIX printf semantics remain open. Process/system utilities `/usr/bin/kill`, `/usr/bin/ps`, `/usr/bin/uname`, and `/usr/bin/du` are QEMU-validated for current-PID, fixed system identity, stat-based file size, and error scenarios; full process enumeration, recursive disk accounting, df/free/dmesg data APIs, and mount operations remain blocked by missing kernel interfaces.

---

# PHASE 20 — Users, Groups, Credentials and Security Model

- UID/GID.
- supplementary groups.
- root/superuser policy.
- credential objects.
- file ownership/modes.
- ACL architecture where justified.
- privilege transitions.
- secure password storage.
- authentication/session framework.
- login/lock/logout.
- capability/least-privilege model.
- environment sanitization.
- setuid/setgid policy if implemented.
- audit identity.

### Security foundation
- W^X.
- NX/WP.
- SMEP/SMAP.
- ASLR.
- stack protections where compatible.
- syscall filtering architecture.
- secure boot/signature verification architecture where feasible.
- secrets handling.

---

# PHASE 21 — Full Network Stack

### Link/network
- Ethernet framing.
- ARP.
- IPv4.
- ICMP.
- UDP.
- TCP state machine.
- routing table.
- interfaces.
- MTU.
- checksums.
- packet buffers.
- socket layer.
- blocking/nonblocking I/O.
- DNS resolver.
- DHCP architecture.
- loopback.
- firewall/filtering architecture.
- IPv6 architecture and later implementation.

### RTL8125
- PCI probe.
- MMIO.
- MAC setup.
- DMA rings.
- TX/RX.
- interrupts.
- link negotiation/state.
- statistics.
- reset/recovery.

### Regression
Manual ping and automated network tests must use the same real driver/socket path. A self-test timeout cannot be replaced with fabricated success.

---

# PHASE 22 — libc, musl, POSIX and Linux Compatibility

### libc
- syscall wrappers;
- `errno`;
- memory/string;
- stdio;
- files;
- process APIs;
- time;
- signals;
- threads;
- sockets;
- locale/UTF-8 foundations;
- TLS.

### musl
- dedicated RixuriOS sysroot;
- architecture configuration;
- syscall layer;
- startup objects;
- dynamic linker;
- pthread integration;
- regression suite.

### Compatibility
- POSIX semantics where practical;
- selected Linux/glibc ABI compatibility shims;
- porting notes for incompatible behavior;
- application compatibility test corpus.

Architecture must remain:

`kernel ABI → RixuriOS libc → POSIX/Linux compatibility → application`.

---

# PHASE 23 — Dynamic Linking, TLS and Runtime Loader

- ELF shared objects.
- `PT_DYNAMIC`.
- symbol lookup.
- relocation processing.
- GOT/PLT.
- lazy/immediate binding policy.
- dependency loading.
- `DT_NEEDED`.
- SONAME.
- TLS models.
- `dlopen/dlsym/dlclose` architecture.
- loader security.
- library search paths.
- versioned symbols where needed.

### Gate
A real dynamically linked application starts, resolves libraries, performs TLS initialization and exits normally.

---

# PHASE 24 — init, Services, devfs, procfs, sysfs and IPC Bus

### PID 1
- first userspace process;
- service dependency graph;
- startup ordering;
- restart policy;
- supervision;
- shutdown.

### Pseudo-filesystems
- `/dev` device nodes.
- `/proc` processes, memory, CPU, mounts, uptime.
- `/sys` devices, drivers, attributes.

### System services
- logger;
- device manager;
- network manager foundation;
- time synchronization foundation;
- service manager.

### IPC
- Unix sockets;
- service discovery;
- capability/credential propagation.

---

# PHASE 25 — Source-Driven Native Package Manager and Software Lifecycle

Phase 25 defines how RixuriOS turns **user-supplied source code** into a native, verified and transactionally installed `.rix` package. It is not a package-name store and it must not depend on a central catalog for the basic install workflow.

### Accepted Inputs

The package manager command is `rix` and accepts either a Git repository URL or an existing local source tree.

Examples:

`rix install https://github.com/user/project.git`

`rix install https://gitlab.com/user/project.git`

`rix install ./my-project`

`rix install /home/vey/my-project`

The source itself is authoritative for the build. A package name such as `firefox` is not required to locate software.

### Source Acquisition

For Git sources, `rix` must:

- validate and fetch supported Git URLs;
- resolve the requested revision/version deterministically;
- record repository URL, revision and source provenance;
- verify source integrity where checksums/signatures are available;
- keep a source cache without bypassing verification.

For local sources, `rix` must:

- operate on the supplied directory without requiring a repository;
- inspect the source tree safely;
- avoid modifying the source tree unless the build system explicitly requires an allowed generated-file step;
- support a disposable/copy-based build mode for isolation.

### Build-System Detection

`rix` must inspect the source tree and select an explicitly supported build backend, including as applicable:

- `Makefile` → Make;
- `CMakeLists.txt` → CMake;
- `meson.build` → Meson;
- `configure`/Autotools files → Autotools;
- `Cargo.toml` → Cargo;
- `go.mod` → Go;
- Python build metadata → supported Python build backend.

Detection must not mean blindly executing arbitrary files. Each backend has a defined command model, environment, dependency discovery rules and safety policy.

### Recursive Dependency Resolution

Dependency resolution is mandatory and must operate on the complete transitive graph.

The resolver must:

- parse direct build/runtime dependencies exposed by the source/build metadata;
- recursively resolve dependencies of dependencies to arbitrary depth;
- support dependency sources as Git/local source inputs and later repository-provided metadata where implemented;
- deduplicate shared dependencies;
- detect dependency cycles;
- detect incompatible version constraints;
- resolve compatible constraints deterministically;
- construct the complete dependency graph before changing the live system;
- calculate a valid topological build/install order.

Example:

`Firefox → A → B → C → D`

must account for **A, B, C and D**, not only A.

If the graph cannot be satisfied, `rix` must abort before modifying the installed system.

### Native RixuriOS Build Environment

Every package must be built against the RixuriOS userspace ABI and native sysroot.

The build environment must:

- use the RixuriOS compiler/toolchain;
- use the RixuriOS libc/sysroot;
- prevent accidental host Linux headers, libraries and pkg-config data from leaking into the build;
- provide deterministic environment variables and paths;
- isolate build outputs and temporary files;
- distinguish native RixuriOS APIs from Linux/glibc/systemd APIs;
- reject unsupported kernel/userspace requirements;
- record the exact build inputs and toolchain identity.

A Linux binary must never be treated as a native RixuriOS package merely because it has a compatible filename or architecture.

### Package Format

Every successful build produces a native `.rix` package containing at minimum:

- package name and source identity;
- version/revision;
- architecture;
- RixuriOS ABI compatibility information;
- runtime dependencies;
- build dependencies;
- installed file manifest;
- checksums;
- source URL/provenance and revision where applicable;
- build metadata;
- package format version.

### Build and Package Cache

Cache:

- fetched source;
- dependency metadata;
- completed native dependency builds;
- generated `.rix` packages.

Cached artifacts must be integrity-verified and must never bypass compatibility or dependency checks.

### Transactional Installation

Installation must be transactional. Before changing the live system, `rix` must:

1. resolve the complete dependency graph;
2. verify dependency constraints and package metadata;
3. build all missing native dependencies;
4. generate and verify all required `.rix` packages;
5. calculate the complete filesystem operation set;
6. detect conflicts with files already owned by installed packages or protected system paths;
7. prepare the transaction;
8. commit atomically where possible.

A failed operation must not leave a partially installed dependency tree.

### Rollback and Recovery

The package manager must provide:

- transaction logs;
- pre-install state capture;
- rollback of failed transactions;
- recovery after interrupted installation;
- resulting filesystem verification;
- integration with the RixuriOS checkpoint/recovery system.

If any transaction step fails, the live system must return to its pre-transaction state to the extent guaranteed by the transaction model.

### Repository and Trust Model

Repositories are optional sources for distributing already-built metadata/packages and for future convenience, not a prerequisite for source-driven installation.

When repositories are used, support:

- repositories and mirrors;
- package indexes;
- version/revision selection;
- metadata signatures;
- trusted keys/root configuration;
- checksum verification;
- package/source provenance.

Untrusted or corrupted metadata must fail closed.

### Safety and Compatibility Gate

A source is installable only when its complete dependency, build and runtime requirements are compatible with RixuriOS.

Unsupported requirements such as:

- Linux kernel-only APIs;
- glibc-only interfaces;
- systemd-only integration;
- unsupported syscalls;
- unsupported filesystem assumptions;
- unsupported compiler/runtime requirements

must produce a deterministic failure with no live-system modification.

### Acceptance Workflow

The canonical Phase 25 workflow is:

`Git URL or local source → source analysis → build-system detection → complete dependency graph → recursive dependency resolution → version/cycle validation → build order → native RixuriOS build → .rix → verification → transactional install → checkpoint`.

A large real-world source tree such as Firefox is an end-to-end acceptance target. It is not a claim that Firefox will build before the underlying libc, dynamic loader, graphics, networking, toolchain and other required platform phases are complete.

### Checkpoints

`P25-01` input/source model → `P25-02` source acquisition → `P25-03` build-system detection → `P25-04` recursive dependency graph → `P25-05` version solving/cycle detection → `P25-06` native Rix build environment → `P25-07` `.rix` generation → `P25-08` transactional install → `P25-09` rollback/recovery → `P25-10` repository/trust model → `P25-11` end-to-end source installation.

No package-manager checkpoint is complete merely because a package can be copied into `/bin`; every gate requires real build/install/failure evidence appropriate to the checkpoint.

---

# PHASE 26 — Developer Platform and Build Ecosystem

### Native development
- GCC/binutils or equivalent.
- Clang/LLVM.
- debugger support.
- assembler/linker.
- make/ninja-like build tooling.
- pkg-config-like metadata.
- C/C++ headers.
- static/dynamic libraries.

### Languages roadmap
- C first;
- C++;
- Rust;
- Go;
- Python runtime;
- JavaScript/Node ecosystem where resources permit.

### Developer workflow
`clone → configure → build → test → install → run → debug → package`.

### Debugging
- symbols;
- core dumps architecture;
- backtraces;
- kernel crash dumps;
- serial capture;
- GDB remote debugging;
- deterministic QEMU snapshots.

---

# PHASE 27 — Mayo, Documentation, Observability and System Administration

### Mayo
- open/save/save-as;
- cursor/navigation;
- insert/delete;
- search;
- line handling;
- UTF-8;
- terminal rendering;
- keyboard integration;
- crash-safe save/recovery.

The editor name is **Mayo**; do not reintroduce the old RixEdit name.

### Observability
- structured kernel logs;
- log levels/categories;
- ring buffer;
- persistent logs;
- tracepoints;
- syscall tracing;
- scheduler tracing;
- block/network/USB traces;
- crash reports;
- health/status commands.

### Administration
- configuration files;
- hostname;
- users;
- services;
- network configuration;
- mount management;
- logs;
- diagnostics;
- recovery shell.

---

# PHASE 28 — Audio and Media Services

- audio device model;
- HDA-oriented driver;
- PCM;
- DMA/ring buffers;
- mixer/control API;
- playback/capture;
- latency measurement;
- device hotplug/recovery;
- userspace audio API.

Also prepare media abstractions for future video/camera support without coupling them to GUI.

---

# PHASE 29 — Graphics Device Foundation and Vulkan

**Still pre-GUI.** Graphics infrastructure must be mature before a desktop exists.

### Display
- GOP handoff.
- framebuffer abstraction.
- modes/pixel formats.
- scanout buffers.
- display hotplug architecture.

### GPU memory
- VRAM.
- system/GTT memory.
- DMA.
- mappings.
- synchronization.
- fences/events.
- resource lifetime.

### QEMU GPU
Keep QEMU `1B36:0100` completely distinct from physical AMD paths.

### AMD
Target RX 6800 XT `1002:73BF` with real capability discovery, VRAM/GTT, queues/rings, interrupts, fences, submission and reset/recovery.

### Vulkan
- loader;
- ICD boundary;
- physical/logical devices;
- queue families;
- memory allocation;
- buffers/images;
- descriptors;
- command buffers;
- synchronization;
- pipeline/shader interfaces;
- validation/diagnostics;
- device-loss recovery.

No Vulkan feature may be advertised unless the underlying hardware/software path is actually implemented and tested.

---

# PHASE 30 — Reliability Engineering, Fuzzing, Fault Injection and Recovery

This phase exists **before GUI** specifically to prevent a fragile desktop being built on a fragile OS.

### Fuzzing targets
- ELF parser;
- filesystem parser;
- path resolver;
- syscall arguments;
- USB descriptors/HID reports;
- network packets;
- configuration files;
- package metadata.

### Fault injection
- allocation failure;
- DMA mapping failure;
- device timeout;
- malformed completion;
- unplug/reset;
- disk I/O error;
- packet loss/reordering;
- process exhaustion;
- FD exhaustion;
- corrupted filesystem metadata.

### Recovery
- driver reset;
- service restart;
- filesystem recovery;
- network reconnect;
- shell recovery;
- safe reboot;
- crash dump preservation.

---

# PHASE 31 — Performance, Scalability and Resource Governance

Measure before optimizing:

- syscall latency;
- context-switch latency;
- scheduler throughput;
- page-fault cost;
- allocator throughput;
- filesystem IOPS/latency;
- NVMe queue performance;
- network throughput/latency;
- TCP connection rate;
- USB transfer latency;
- shell startup;
- dynamic-loader startup;
- memory footprint;
- boot time.

Add resource limits:

- process/thread limits;
- FD limits;
- address-space limits;
- memory accounting;
- disk quotas architecture;
- socket/packet limits;
- service resource policies.

No optimization may weaken isolation or correctness without an explicit architectural decision.

---

# PHASE 32 — Installer, Recovery Environment and Update System

### Installer
- disk/partition discovery;
- filesystem creation;
- EFI System Partition;
- bootloader installation;
- system image deployment;
- userspace setup;
- boot configuration;
- verification.

### Safety
Formatting, partition deletion and disk overwrite require explicit confirmation. Installer must identify the exact target device and refuse ambiguous targets.

### Recovery
- boot recovery menu;
- rescue shell;
- filesystem check;
- logs;
- rollback;
- backup/restore.

### Updates
- atomic system updates where feasible;
- bootable previous version;
- package transaction recovery;
- version compatibility checks.

---

# PHASE 33 — Real Hardware Qualification Lab

Before GUI, qualify the entire platform on target hardware.

### Required qualification classes
- UEFI firmware variations.
- CPU feature variations.
- multi-core boot.
- target NVMe.
- USB controller and HID.
- RTL8125.
- RX 6800 XT identification/graphics foundation.
- audio hardware.
- suspend/reboot/power behavior.

### Evidence
Every hardware test records:

`machine identity + firmware version + PCI inventory + kernel build + test command + raw log + result + known limitations`.

QEMU evidence never substitutes for physical evidence.

---

# PHASE 34 — Pre-GUI Platform Certification

This is the **largest gate in the project**.

The system must be usable entirely without a GUI:

```text
UEFI
 ↓
kernel
 ↓
memory / interrupts / SMP
 ↓
scheduler / processes / syscalls
 ↓
NVMe / RixFS / VFS
 ↓
USB / keyboard / TTY / PTY
 ↓
init / users / permissions
 ↓
network / sockets / DNS
 ↓
musl / POSIX / dynamic linking
 ↓
shell / coreutils
 ↓
package manager / developer tools
 ↓
Mayo / diagnostics / recovery
```

### Certification suites
- boot suite;
- memory suite;
- CPU/SMP suite;
- process/scheduler suite;
- syscall suite;
- ELF/runtime suite;
- storage/NVMe suite;
- filesystem/fsck suite;
- USB/HID suite;
- TTY/shell suite;
- authentication suite;
- network suite;
- libc/POSIX suite;
- package/toolchain suite;
- audio suite;
- graphics/Vulkan foundation suite;
- security suite;
- recovery suite;
- performance suite;
- long-duration soak test.

### Gate
No GUI development begins merely because the framebuffer works. Phase 34 must certify that the OS is already a useful Unix-like computer from the terminal.

---

# PHASE 35 — GUI / Desktop (ABSOLUTE LAST)

Only after Phase 34 is released.

### Display server/compositor
- display discovery;
- modes;
- surfaces;
- buffers;
- composition;
- synchronization;
- GPU/Vulkan integration;
- multi-monitor architecture.

### Window system
- windows;
- focus;
- input routing;
- clipboard;
- drag/drop;
- decorations;
- workspaces;
- virtual desktops.

### Desktop services
- session manager;
- launcher;
- settings;
- notifications;
- authentication UI;
- power UI;
- network UI;
- audio UI;
- file manager.

### Toolkit
- text/fonts;
- widgets;
- layout;
- accessibility;
- theming;
- internationalization.

### Critical property
A compositor crash must not destroy the kernel, userspace or filesystem. The system must fall back to the terminal/recovery environment.

---

# Release ladder

- **R0 — Firmware Boot:** UEFI → validated kernel.
- **R1 — Protected Kernel:** memory + exceptions + interrupts.
- **R2 — SMP Kernel:** timers + APIC + multicore + synchronization.
- **R3 — Multitasking:** scheduler + real user process + syscalls.
- **R4 — Storage OS:** block layer + NVMe + VFS + RixFS.
- **R5 — Unix CLI:** TTY/PTY + shell + coreutils.
- **R6 — Networked Unix:** users + sockets + TCP/IP + RTL8125.
- **R7 — Developer Unix:** musl + POSIX + dynamic linking + toolchain.
- **R8 — Managed OS:** init + services + package manager + recovery.
- **R9 — Hardware Platform:** audio + GPU/Vulkan foundation + qualified hardware.
- **R10 — Certified Pre-GUI OS:** Phase 34 complete.
- **R11 — Graphical OS:** Phase 35 complete.

## Definition of done

A feature is complete only when its evidence demonstrates:

`correct implementation + real execution + integration + failure handling + regression coverage + security review + measured behavior + documentation`.

The detailed checkpoint ledger belongs in `docs/CHECKPOINTS.md`; implementation procedures belong in `docs/IMPLEMENTATION_PLAYBOOK.md`.


### Phase 19 continuation — 2026-09-06

The next increment added `/usr/bin/find`, `/usr/bin/xargs`, `/usr/bin/sed` and `/bin/test` over the existing libc and syscall ABI. All four have strict freestanding ELF builds and RixFS image entries. `find` uses recursive `stat`/`getdents`; `sed` uses bounded basic substitution; `test` returns defined 0/1/2 results for its supported forms; and `xargs` uses bounded tokenization plus real fork/exec/wait. A real QEMU boot reached the mounted NVMe/RixFS shell, but the dedicated utility harness lost the prompt at the xargs pipeline. The correct status is therefore `IMPLEMENTED / NOT YET VALIDATED`, not COMPLETE.

`df`, `free`, `dmesg`, `mount` and `umount` are explicitly deferred. The next kernel design step is a versioned `statfs`/mount-accounting ABI, a privilege-checked kernel-log ring reader, a `sysinfo`-style memory snapshot, and mount namespace/device operations with ownership, rollback, and corrupt-media failure semantics. No utility may report fabricated values until those APIs and their negative/QEMU tests exist.


The provisional syscall/data-model design for this work is maintained in [`docs/PHASE19_KERNEL_API.md`](PHASE19_KERNEL_API.md). It is intentionally separate from implementation status so that API review can happen before any utility reports data.


### xargs runtime regression follow-up

Before closing the xargs gate, add a focused disposable-image QEMU regression for `fork → execve → wait` with one inherited pipe descriptor and no xargs parsing. Capture PMM allocation/free ownership for every address-space page-table level, verify bootstrap VMM tables cannot be returned by the allocator, and verify the child’s exec replacement leaves inherited descriptors and the parent’s CR3 valid. Then rerun the full xargs pipeline harness.


### Minimal fork-return regression

Add a standalone QEMU case where the second-fork child executes only `_exit(7)`. Log the syscall ISR frame pointer, saved user RIP/RSP, and the exact five-word `iretq` frame immediately before `x86_enter_user_context`. Compare parent and child kernel-stack bounds and verify no user buffer write overlaps the frame. This must pass before returning to nested `execve` and xargs.


### Fork/address-space redesign

Implement the phased design in [`docs/FORK_ADDRESS_SPACE_DESIGN.md`](FORK_ADDRESS_SPACE_DESIGN.md): first introduce a permanent kernel page-table mapping window, then ownership journals and transactional clone rollback, followed by kernel-stack/scratch-storage hardening and the minimal fork-return regression. Only after those gates pass should xargs be re-enabled as the final pipeline acceptance test.


Phase A has started with a checked `vmm_phys_ptr()` access boundary. The next implementation step is to back this boundary with a permanent mapped kernel window; the current identity fallback is deliberately not considered a fork/xargs fix.


### Current evidence — 2026-09-07

The RixFS/VFS rename path now has a bounded redo-journal transaction for regular-file moves. Real QEMU/NVMe evidence passed same-directory inode-preserving rename, overwrite replacement, rename-to-directory failure safety, cross-directory move, cross-directory overwrite, and round-trip movement. Deleted directory sectors are skipped consistently by lookup, readdir, remove, and rename discovery, so repeated create/remove and transactional rename operations do not fail on reusable holes. The cp/mv edge harness also passed empty-file movement, metadata-preserving fallback, overwrite, and multi-sector executable copy/readback.

The Phase 20 authentication path now passes the complete QEMU credential/session/authentication suite, including account add, password rotation, verification with the new password, login session creation and teardown, old-password rejection, protected shadow access, account removal, and account-count checks. A rotation preserves `/etc/passwd` and replaces only the requested `/etc/shadow` record. These results are QEMU-validated on the disposable NVMe-backed image; physical NVMe behavior, power-loss injection during commit, directory rename semantics, symlinks, and broader policy matrices remain open.


### Current evidence — 2026-09-07

The Phase 20 QEMU credential regression now covers not only owner/group/other regular-file access but also path-search and parent-mutation denial through a root-owned mode-0700 directory. After dropping to UID/GID 1000, open, stat, child creation and unlink through that inaccessible path return the documented permission error, while missing paths remain distinguishable. Group-owned mode-0640 read access through supplementary groups and write denial continue to pass, together with persisted owner/group/mode metadata checks. The complete credential, capability, session and account/authentication suites pass on the disposable NVMe-backed image. Broader policy matrices, physical hardware security evidence and remaining Phase 20 boundaries are still open.


### Current evidence — 2026-09-07

The bounded account store now supports reversible lock and unlock operations. A locked shadow record fails password verification and login while preserving its hash; unlock restores verification. The real QEMU auth harness also attempts a rotation for an unknown account, requires the command to fail, and immediately verifies that the existing operator password remains valid before continuing with a successful rotation/login and old-password rejection. This closes the bounded lock/unlock and invalid-input no-mutation slice, but not power-loss injection or a full interactive login manager.


### Current evidence — 2026-09-07

The set-ID exec regression now supplies a deliberately tainted environment and requires the privileged target to receive an empty environment while still completing the UID/GID transition. This provides real QEMU evidence for the privileged-environment sanitization branch; a complete environment-management subsystem and physical security qualification remain outside the bounded Phase 20 model.


### Current evidence — 2026-09-07

Authentication now verifies the password, creates a session, transitions the session leader to the account UID/GID, verifies that identity, and logs out without requiring retained administrative capabilities. The QEMU regression requires a separate login-identity marker. The permission matrix also validates group-owned directory child creation, readback and unlink through supplementary-group search/write/execute access. Full rmdir policy coverage remains a distinct filesystem boundary; hardware and crash-injection gates remain unavailable in this environment.


### Current evidence — 2026-09-07

The shell prompt is now active as a colored dynamic `username@hostname directory :` presentation. It derives the username from the process UID, uses the `rixurios` machine label and reads cwd at each redraw. The Phase 19 QEMU regression requires both the colored root/hostname marker and the colored `/p19ext` marker after directory change; prompt text is not used as an authorization source.
