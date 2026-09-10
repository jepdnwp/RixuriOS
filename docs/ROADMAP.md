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

---

# PHASE 18 — Shell, Job Control and Command Execution

- shell parser;
- quoting/escaping;
- pipelines;
- redirections;
- environment expansion;
- globbing;
- command lookup;
- exit status;
- foreground/background jobs;
- process groups;
- controlling terminal;
- signal-aware job control.

### Gate
A real user can launch, pipe, redirect, background and terminate processes without kernel bypasses.

---

# PHASE 19 — Unix Utilities and Base Userland

Provide a coherent terminal-first base userland covering core filesystem, process, text, diagnostics and system utilities. Utilities must use the documented RixuriOS ABI rather than private kernel interfaces.

### Initial utility families
- filesystem navigation and manipulation;
- process inspection/control;
- text and stream processing;
- terminal utilities;
- system information;
- storage inspection;
- networking diagnostics;
- basic administrative utilities.

### Gate
Utilities execute as normal userspace programs, return meaningful exit codes, handle errors and do not assume Linux-specific kernel behavior.

---

# PHASE 20 — Users, Groups, Credentials, Sessions and Security Policy

- UID/GID model.
- supplementary groups.
- credential propagation.
- file permission model.
- ACL architecture.
- capability architecture where justified.
- login/session model.
- controlling terminal ownership.
- privilege separation.
- audit events.
- security policy configuration.
- ASLR/stack-hardening integration.

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
- retransmission.
- ordering.
- flow/window control.
- checksums.
- socket API integration.
- blocking/nonblocking receive/transmit semantics.
- DNS resolver architecture.

### Drivers
- E1000/QEMU reference path.
- RTL8125 `10EC:8125`.
- device reset and recovery.
- RX/TX rings.
- interrupt/MSI-X path where supported.

### Evidence
Loopback success, guest-to-host packets, external connectivity and physical RTL8125 evidence are tracked separately.

---

# PHASE 22 — libc, POSIX Compatibility and C Runtime Surface

### libc
- freestanding standard headers;
- errno and standard constants;
- string/memory routines;
- stdio streams;
- formatted I/O;
- scanf-family subset;
- allocation APIs;
- environment APIs;
- ctype;
- time APIs;
- directory APIs;
- process APIs;
- POSIX filesystem wrappers;
- sockets;
- signals;
- pthread synchronization surface;
- locale/wchar compatibility;
- utility APIs such as getopt/sysconf/getpagesize.

### POSIX compatibility
Maintain the compatibility matrix with explicit implemented/partial/stub/missing states. ENOSYS is acceptable only where the documented ABI intentionally has no implementation yet and the behavior is tested.

### musl trajectory
Provide a documented syscall/ABI compatibility layer and bootstrap sysroot shape without claiming a full musl port until dynamic linking, TLS, threading and the required syscall surface exist.

### Gate
Static compatibility tests pass in host and QEMU scopes, with deferred dynamic-linking/TLS/hardware evidence explicitly recorded.

---

# PHASE 23 — Dynamic ELF Loader, Shared Libraries and TLS

### ELF dynamic execution
- PT_INTERP handling;
- PT_DYNAMIC parsing;
- dynamic section validation;
- GOT/PLT relocation;
- REL/RELA processing;
- symbol lookup;
- `DT_NEEDED` dependency loading;
- SONAME resolution;
- shared-library search paths;
- dynamic linker entry;
- executable/interpreter ABI contract;
- secure loader failure handling.

### Dynamic APIs
- `dlopen`;
- `dlsym`;
- `dlclose`;
- `dlerror`.

### TLS
- PT_TLS parsing;
- static TLS layout;
- dynamic TLS architecture;
- `%fs` thread pointer setup;
- TLS relocation models;
- loader/thread handoff contract.

### Gate
A genuinely dynamically linked userspace program loads at boot, resolves shared-library dependencies and accesses TLS without Linux-specific shortcuts.

---

# PHASE 23A — Rix Privileged Command Interface

`rix` is the native RixuriOS privileged/system-management command interface. It is inspired by the operational role of `sudo`, but is designed around the RixuriOS syscall ABI and privilege model rather than copying Linux `sudo` semantics.

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

### Command architecture

```text
user
 ↓
rix
 ↓
argument parser
 ↓
authorization policy
 ↓
RixuriOS syscall ABI
 ↓
privileged kernel subsystem
```

The command dispatcher must use a stable internal command model, common argument/error handling, documented exit statuses and auditable operation records.

### Authorization

- UID/GID-based privilege policy.
- root/administrator policy.
- command-specific authorization.
- kernel-enforced privilege boundaries.
- fail-closed `EPERM`/authorization errors.
- no implicit privilege through `PATH`.
- no trusted execution of attacker-controlled relative paths.
- safe environment handling.
- controlled child environment and file-descriptor inheritance.
- symlink/path-traversal and TOCTOU review.
- successful and failed authorization audit records.

### Power management

```text
rix shutdown
rix poweroff
rix reboot
rix halt
```

These commands must flow through the documented syscall ABI into the kernel power-management layer and then into ACPI/platform mechanisms where available. Userspace must not directly access ACPI or platform hardware registers for privileged power operations.

### Service management

```text
rix service start <name>
rix service stop <name>
rix service restart <name>
rix service status <name>
```

The first implementation may be minimal, but unsupported service-manager operations must fail explicitly rather than reporting fake success.

### Administration surface

The interface may grow to include:

```text
rix user add
rix user del
rix user passwd

rix group add
rix group del

rix network status
rix network up
rix network down

rix storage list
rix storage info
```

Every added subcommand requires its own authorization, error semantics and tests.

### Diagnostics

`rix status` and `rix diagnostics` should expose useful system state through documented interfaces without requiring private kernel structures or unsafe memory access.

### Security requirements

Test and review:

- malicious `PATH`;
- malicious environment variables;
- relative executable paths;
- symlink attacks;
- path traversal;
- TOCTOU races;
- malformed/oversized arguments;
- invalid UID/GID values;
- unauthorized users;
- repeated authorization failures;
- inherited file descriptors;
- privilege retention/drop behavior;
- signal handling;
- concurrent invocations;
- shutdown/reboot race conditions.

`rix` must not become a privilege-escalation mechanism.

### Evidence

QEMU and physical-hardware evidence are separate classes. Successful QEMU power operations do not establish physical hardware qualification.

### Checkpoints

`P23A-01` dispatcher → `P23A-02` authorization → `P23A-03` audit logging → `P23A-04` `rix status` → `P23A-05` `rix diagnostics` → `P23A-06` `rix shutdown` → `P23A-07` `rix reboot` → `P23A-08` `rix poweroff` → `P23A-09` service interface → `P23A-10` negative/security tests → `P23A-11` QEMU integration → `P23A-12` physical evidence where applicable → `P23A-13` documentation/release checkpoint.

### Definition of Done

Phase 23A is complete only when the command dispatcher, authorization model, kernel integration, real supported operations, positive/negative tests, QEMU integration, required hardware evidence, security review, regression coverage and documentation are complete. Command presence or a printed success message is not evidence of completion.

---

# PHASE 24 — Threads, Futex and Concurrency Runtime

- kernel thread objects;
- user thread creation;
- join/detach;
- thread exit;
- futex-like wait/wake primitive;
- robust synchronization semantics;
- per-thread errno/TLS integration;
- pthread runtime integration;
- scheduler/thread lifetime interaction;
- cancellation architecture.

### Gate
Multiple user threads execute concurrently with defined synchronization, cleanup and failure behavior.

---

# PHASE 25 — Hardened Memory, Fault Recovery and Process Isolation

- real kernel heap reclaim;
- allocator coalescing/slabs;
- guard pages;
- stack guards;
- ASLR;
- SMAP/SMEP policy;
- fault-safe uaccess;
- page-fault recovery policy;
- copy-on-write where justified;
- memory quotas/OOM policy;
- hardened kernel mappings;
- W^X across appropriate privilege domains.

---

# PHASE 26 — Advanced VFS, Filesystem Semantics and Recovery

- symlinks and link-count semantics;
- `fstat`/`lstat` and full stat metadata;
- timestamps;
- file locking;
- rename/unlink corner cases;
- path-cache coherency;
- fsync semantics;
- crash recovery tests;
- power-loss regression;
- filesystem corruption corpus;
- repair/recovery tooling.

---

# PHASE 27 — Advanced Networking and System Services

- complete TCP retransmission/window/congestion behavior;
- DNS resolver;
- blocking socket waits integrated with scheduler;
- service manager;
- logging daemon;
- time synchronization architecture;
- network configuration management;
- local IPC/service sockets.

---

# PHASE 28 — GUI and Graphics Stack

GUI remains absolutely last and cannot consume engineering capacity while pre-GUI release gates are open.

### Graphics
- framebuffer abstraction;
- graphics memory management;
- modesetting architecture;
- GPU command submission architecture;
- synchronization/fences;
- display pipeline;
- cursor;
- input integration.

### Windowing
- compositor;
- windows/surfaces;
- event loop;
- keyboard/mouse routing;
- terminal emulator client;
- application lifecycle.

### GPU targets
- QEMU reference graphics path;
- AMD RX 6800 XT `1002:73BF` hardware qualification;
- acceleration only after stable software rendering/reference path.

### Gate
GUI is a consequence of a stable pre-GUI OS, not a substitute for one.

---

# PHASE 29 — Package, Toolchain and Developer Environment

- package format;
- repository/index format;
- dependency resolution;
- signed metadata;
- compiler/toolchain distribution;
- debugger support;
- profiler/tracing tools;
- developer SDK;
- reproducible package builds.

---

# PHASE 30 — Installer, Recovery Environment and System Lifecycle

- installer UI/CLI;
- disk partitioning safety;
- ESP setup;
- filesystem creation;
- bootloader installation;
- upgrade/rollback;
- recovery environment;
- rescue shell;
- backup/restore architecture;
- migration handling.

---

# PHASE 31 — Security Hardening and Audit

- threat model refresh;
- privilege review;
- syscall fuzzing;
- parser fuzzing;
- filesystem fuzzing;
- network fuzzing;
- driver fault injection;
- DMA/IOMMU review;
- secret handling;
- secure boot/signing architecture;
- exploit regression corpus.

---

# PHASE 32 — Performance, Reliability and Soak Testing

- boot-time measurement;
- syscall latency;
- scheduler latency;
- storage throughput/latency;
- network throughput/latency;
- allocator performance;
- memory pressure;
- long-run soak tests;
- power-cycle loops;
- suspend/resume loops;
- crash-free endurance criteria.

---

# PHASE 33 — Physical Hardware Qualification

### Required evidence
For every supported physical target record:

- machine identifier;
- motherboard/firmware;
- CPU;
- memory;
- storage controller/device;
- PCI inventory;
- network controller;
- USB controller;
- GPU/display device;
- boot mode;
- kernel configuration;
- raw serial/framebuffer log;
- test command;
- observed result;
- regression status.

### Initial target evidence
Explicitly qualify, where present:

- RTL8125 `10EC:8125`;
- NVMe device;
- xHCI controller;
- USB HID keyboard/mouse;
- AMD RX 6800 XT `1002:73BF`;
- PCIe/MSI-X behavior.

QEMU results may support development but cannot close physical-hardware gates.

---

# PHASE 34 — Release Candidate, Compliance and Pre-GUI Gate

### Release gates
- all mandatory phases complete or explicitly waived;
- complete regression suite;
- security review closed;
- hardware matrix reviewed;
- reproducible build artifacts;
- release notes;
- known-limitations register;
- rollback/recovery procedure;
- signed release artifacts where supported.

### Pre-GUI rule
No GUI milestone may be considered complete until the required pre-GUI gates are closed with evidence.

---

# PHASE 35 — Graphical OS Productization

### Product
- desktop/session model;
- user-facing settings;
- display manager/login UI;
- terminal emulator;
- system monitor;
- file manager;
- networking UI;
- storage UI;
- power UI;
- accessibility infrastructure.

### Quality
- usability testing;
- crash recovery;
- update lifecycle;
- hardware compatibility matrix;
- documentation;
- release engineering.

GUI must remain downstream of the verified kernel, userspace, networking, storage, security and hardware platform.
