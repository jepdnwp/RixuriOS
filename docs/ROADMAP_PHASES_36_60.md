# RixuriOS Extended Roadmap — PHASE 36–60

This document is an additive extension to `docs/ROADMAP.md`. It intentionally does **not** replace or shorten the existing Phase 00–35 roadmap. These phases begin after the graphical productization gate and cover the engineering required to turn RixuriOS from a working OS into a complete, maintainable, secure and broadly usable platform.

## Extension rules

- Existing Phase 00–35 requirements remain authoritative.
- A later phase may not declare an earlier dependency complete by assumption.
- `PASS` requires reproducible evidence; source inspection alone is not hardware evidence.
- Every driver and subsystem needs QEMU, negative, timeout, reset/recovery and physical-hardware evidence where applicable.
- No silent fallback to fake hardware, synthetic packets, fabricated storage or test-only success paths.
- Security, observability and recovery are first-class requirements rather than final cleanup.
- Every phase ends with a checkpoint ledger update, regression run and documented limitations.

---

# PHASE 36 — Production SMP, NUMA and CPU Topology

### CPU topology
- Enumerate package/core/thread topology from ACPI/CPUID.
- Per-CPU data and per-CPU allocators.
- CPU hotplug state machine.
- CPU online/offline transitions.
- CPU affinity and isolation.
- topology-aware scheduler domains.
- NUMA node discovery and memory locality.

### SMP correctness
- Cross-CPU interrupt delivery.
- IPI types and ownership.
- TLB shootdown batching.
- Remote function calls.
- stop-the-world coordination.
- RCU-compatible read-side primitives where justified.
- lock contention instrumentation.
- interrupt migration rules.

### Evidence
`P36-01` 2 CPUs → `P36-02` 4 CPUs → `P36-03` concurrent syscall load → `P36-04` TLB shootdown stress → `P36-05` CPU offline/online → `P36-06` NUMA topology report.

---

# PHASE 37 — Advanced Virtual Memory and Memory Pressure

### VM
- Demand paging.
- Anonymous memory.
- File-backed mappings.
- Copy-on-write.
- Shared/private mapping semantics.
- `mmap`, `munmap`, `mprotect`, `brk` and related ABI completion.
- Guard regions.
- VMA interval management.
- Page-cache integration.
- Huge-page policy.

### Reclamation
- Physical page reference accounting.
- LRU/working-set model.
- Reclaim under pressure.
- OOM detection and policy.
- Per-process memory limits.
- Kernel memory accounting.
- Zero-page optimization where useful.

### Fault handling
- Not-present faults.
- protection faults.
- copy-on-write faults.
- executable/write policy violations.
- stack growth policy.
- bad-access termination without corrupting the kernel.

### Evidence
OOM, fork-heavy COW, mmap fragmentation, unmap/re-map, concurrent page faults and page-cache pressure must all have deterministic regression tests.

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
- foreground/background process control.
- terminal-generated signals.
- process resource accounting.
- per-process limits.
- environment and argument lifetime guarantees.
- safe exec transition.

### Stress
Thousands of short-lived processes, fork/exec storms, simultaneous exits and parent death must not leak PIDs, file descriptors, VMAs or kernel objects.

---

# PHASE 39 — Signals, Timers and Asynchronous Event Model

### Signals
- complete signal set policy.
- signal masks.
- pending queues.
- standard vs queued signals.
- process-directed and thread-directed delivery.
- alternate signal stack.
- signal frame ABI.
- `sigreturn` validation.
- interrupted syscall restart policy.
- signal disposition inheritance/reset across exec.
- synchronous faults.

### Timers
- per-process timers.
- interval timers.
- timerfd-like interface if adopted.
- realtime timer queue.
- cancellation races.
- clock-source correctness.

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
- scheduler interaction with blocked threads.
- priority inversion diagnostics.
- fork/exec behavior in multithreaded processes.

### Evidence
Run real multithreaded C programs with contention, cancellation, signals, fork, exec, TLS and high thread counts.

---

# PHASE 41 — Dynamic ELF, TLS and Runtime Linker Completion

- PT_INTERP.
- PT_DYNAMIC.
- DT_NEEDED.
- DT_SONAME.
- DT_RPATH/DT_RUNPATH policy.
- symbol tables and hash tables.
- relocations.
- REL/RELA handling.
- GOT/PLT.
- lazy/immediate binding policy.
- symbol versioning architecture.
- PIE.
- shared-object load/unload.
- `dlopen`, `dlsym`, `dlclose`, `dlerror`.
- dependency graph and cycle handling.
- constructor/destructor ordering.
- TLS program headers.
- static TLS allocation.
- dynamic TLS model.
- `%fs` thread pointer setup.
- auxiliary vector completeness.

### Gate
A real dynamically linked userspace binary must execute against RixuriOS's loader and shared libraries without a test-only loader path.

---

# PHASE 42 — musl Port and Full C/POSIX Runtime

- Complete RixuriOS musl syscall layer.
- ABI-specific context and startup files.
- pthread integration.
- TLS integration.
- signals.
- mmap/brk/mprotect.
- file and directory APIs.
- sockets.
- polling.
- clocks/timers.
- process APIs.
- terminal APIs.
- dynamic loader integration.
- locale/timezone requirements selected for the product.
- libc conformance test suite.

### Compatibility matrix
Track each targeted POSIX/libc interface as `implemented`, `partial`, `unsupported`, `known-broken` or `not-tested`, with a test artifact for every promoted status.

---

# PHASE 43 — VFS Completion, Namespaces and Filesystem Semantics

- `stat`, `fstat`, `lstat` completeness.
- timestamps and metadata updates.
- symlinks.
- hard links.
- rename atomicity.
- open flags and mode semantics.
- advisory locking.
- file leases where justified.
- mount namespaces.
- bind mounts.
- read-only mounts.
- mount propagation model.
- path resolution race resistance.
- dentry cache invalidation.
- inode lifetime rules.
- fd inheritance and `CLOEXEC`.
- `dup`/`dup2`/`dup3` semantics.

### Recovery
Unmount failures, dirty filesystems, device disappearance and corrupted metadata must have explicit state transitions and diagnostics.

---

# PHASE 44 — RixFS v2: Journaling, Snapshots and Integrity

- Formal on-disk specification.
- metadata checksums.
- journal transaction model.
- ordered/writeback modes.
- crash-consistency protocol.
- orphan cleanup.
- free-space verification.
- fsck repair classes.
- read-only emergency mount.
- snapshot metadata.
- snapshot creation/deletion.
- copy-on-write snapshot data.
- quota accounting.
- sparse files.
- extended attributes.
- file capabilities metadata where adopted.

### Power-loss matrix
Test interruption during allocation, metadata update, rename, truncate, journal commit, fsync and unmount.

---

# PHASE 45 — Networking: Full IPv4/IPv6 and Socket Semantics

### IP
- IPv4 fragmentation/reassembly.
- IPv6.
- ICMP/ICMPv6.
- neighbor discovery.
- ARP hardening.
- routing tables.
- interface configuration.
- MTU handling.
- multicast basics.

### Transport
- TCP sequence/ack correctness.
- retransmission timers.
- congestion control.
- receive/send windows.
- out-of-order queues.
- duplicate ACK handling.
- FIN/RST state machine.
- TIME_WAIT.
- keepalive.
- UDP edge cases.

### Sockets
- blocking/nonblocking.
- connect/accept/listen.
- shutdown.
- socket options.
- poll/select/epoll-like architecture.
- Unix-domain sockets.
- descriptor passing.

---

# PHASE 46 — Network Services, DNS, DHCP and Diagnostics

- DHCP client/server architecture as required.
- DNS stub resolver.
- UDP/TCP DNS fallback.
- IPv6 DNS behavior.
- `/etc/resolv.conf` equivalent policy.
- hostname configuration.
- routing diagnostics.
- interface statistics.
- packet counters.
- socket inspection.
- network namespace architecture if adopted.
- loopback completeness.
- service startup/shutdown ordering.

### Userland
Provide reliable equivalents for basic network administration and troubleshooting rather than hard-coded demo output.

---

# PHASE 47 — Real Hardware Driver Qualification Program

Create a formal driver qualification framework covering:

- PCIe enumeration.
- MSI/MSI-X.
- DMA/IOMMU.
- NVMe.
- RTL8125.
- E1000/QEMU NIC.
- xHCI.
- USB HID.
- storage hotplug.
- suspend/resume where supported.
- GPU discovery.
- audio hardware.
- thermal/ACPI devices.

For every driver record:
- PCI identity.
- firmware requirements.
- DMA constraints.
- interrupt mode.
- reset procedure.
- timeout values.
- error recovery.
- hotplug behavior.
- known limitations.
- physical PASS/FAIL evidence.

---

# PHASE 48 — GPU, Display and Window-System Foundation

This phase expands the former GUI phase into a real graphics architecture.

### Display
- framebuffer abstraction.
- EDID parsing.
- connector/mode discovery.
- multi-monitor model.
- hotplug detection.
- pixel formats.
- scanout buffers.

### GPU
- PCI discovery.
- VRAM/GTT model.
- command submission architecture.
- synchronization/fences.
- memory management.
- reset/recovery.
- hardware acceleration only after stable modesetting.

### Window system
- compositor architecture.
- surfaces/buffers.
- input routing.
- focus/activation.
- clipboard/selection.
- cursor.
- damage tracking.
- accessibility hooks.

---

# PHASE 49 — Audio and Multimedia Subsystem

- audio device abstraction.
- codec discovery.
- playback/capture streams.
- ring buffers.
- sample-rate conversion policy.
- mixer/volume model.
- underrun/overrun recovery.
- device hotplug.
- latency measurement.
- userland audio service.
- basic media timing primitives.

No audio device is marked supported from PCI detection alone.

---

# PHASE 50 — Power Management, Thermal and Laptop Platform

- ACPI namespace evaluation framework.
- power buttons.
- lid state.
- battery/AC adapter.
- thermal zones.
- fan control where safe.
- CPU frequency policy architecture.
- idle-state selection.
- suspend/resume.
- wake sources.
- device resume ordering.
- NVMe/xHCI/network resume testing.
- crash-safe fallback when firmware methods fail.

### Hardware matrix
Maintain per-platform results rather than a single generic `laptop supported` flag.

---

# PHASE 51 — Security Architecture and Privilege Isolation

- user/kernel W^X.
- SMEP/SMAP enforcement.
- NX everywhere practical.
- hardened usercopy.
- pointer/integer overflow auditing.
- stack protection.
- kernel stack guards.
- KASLR architecture where practical.
- userspace ASLR.
- capability-oriented permission model where adopted.
- fine-grained privileged operations.
- secure path resolution.
- TOCTOU-resistant authorization.
- fd and object lifetime hardening.
- syscall fuzzing.
- driver attack-surface review.
- DMA isolation.
- IOMMU policy.

### Security evidence
Every security mitigation needs a positive test, a bypass attempt and a documented hardware dependency.

---

# PHASE 52 — Cryptography, Entropy and Trust Infrastructure

- hardware entropy sources.
- kernel CSPRNG.
- early-boot entropy strategy.
- cryptographic primitive library boundary.
- constant-time implementation rules.
- key material lifetime/zeroization.
- secure storage architecture.
- hash/integrity primitives.
- authenticated update metadata.
- certificate/time validation dependencies.
- secure random userland API.

Do not roll custom cryptography when a reviewed implementation can be reused safely.

---

# PHASE 53 — Secure Boot, Measured Boot and Update Trust

- UEFI Secure Boot compatibility.
- signed boot artifacts.
- signature verification.
- key rotation policy.
- revoked-key handling.
- measured boot architecture where TPM is available.
- boot-chain measurement logs.
- anti-rollback metadata.
- signed kernel/modules/userspace packages.
- recovery key path.
- offline verification tools.

Failure must stop or enter an explicit recovery path rather than silently booting an untrusted image.

---

# PHASE 54 — Service Manager, Init and System Lifecycle

- PID 1 design.
- service descriptors.
- dependency graph.
- ordering constraints.
- restart policy.
- crash-loop detection.
- readiness/liveness state.
- privilege dropping.
- service sandboxing.
- environment policy.
- resource limits.
- logging integration.
- shutdown transaction graph.
- recovery/single-user mode.

### `rix` integration
`rix service`, `rix status`, `rix diagnostics`, `rix reboot`, `rix poweroff` and related commands must call real kernel/service interfaces with authorization checks.

---

# PHASE 55 — procfs/sysfs/devfs and System Introspection

- `/proc` process model.
- CPU information.
- memory information.
- mounts.
- open files where safe.
- process status.
- `/sys` device hierarchy.
- driver binding state.
- power state.
- `/dev` device nodes.
- major/minor allocation.
- uevent/hotplug architecture.
- stable hardware identifiers.

Diagnostics must expose real state and never fabricate support.

---

# PHASE 56 — Observability, Crash Dumps, Tracing and Debugging

### Logging
- structured kernel logs.
- severity levels.
- timestamps.
- CPU/thread/process IDs.
- rate limiting.
- persistent crash logs.

### Crash analysis
- panic reason.
- register dump.
- stack trace.
- page-fault metadata.
- loaded image/module list.
- recent scheduler/IRQ history.
- storage/network/USB controller state snapshots.
- crash dump persistence and recovery.

### Tracing
- syscall tracing.
- scheduler tracing.
- lock contention.
- IRQ latency.
- block I/O latency.
- network latency.
- userspace profiling hooks.

---

# PHASE 57 — Testing, Fuzzing and Fault Injection Platform

### Automated tests
- unit tests.
- kernel component tests.
- userspace ABI tests.
- libc conformance.
- filesystem tests.
- networking tests.
- driver tests.
- boot tests.
- power-cycle tests.

### Fuzzing
- ELF parser.
- filesystem metadata.
- path resolver.
- syscall arguments.
- USB descriptors/HID reports.
- PCI capabilities.
- ACPI tables.
- network packets.
- terminal escape sequences.

### Fault injection
- allocation failure.
- DMA mapping failure.
- IRQ loss.
- device timeout.
- controller reset.
- corrupted media.
- packet loss/reordering.
- CPU starvation.
- power-loss simulation.

---

# PHASE 58 — Toolchain, SDK, Package Manager and Developer Platform

- reproducible cross compiler.
- sysroot generation.
- libc/toolchain bootstrap.
- assembler/linker/binutils or LLVM integration policy.
- headers and ABI versioning.
- debugger support.
- symbol server/debug package policy.
- package format.
- package metadata.
- dependency solver.
- repository metadata.
- signed package verification.
- install/remove/upgrade/rollback.
- developer SDK image.
- application template.
- documentation generator.

### Goal
A third-party developer should be able to build a real dynamically linked RixuriOS program from a clean host without manually patching the kernel tree.

---

# PHASE 59 — Installer, Recovery, A/B Updates and Disaster Recovery

### Installer
- hardware discovery.
- disk selection safeguards.
- partitioning.
- ESP creation/validation.
- RixFS creation only after explicit confirmation.
- bootloader installation.
- initial user creation.
- network setup.
- timezone/locale setup.

### Recovery
- standalone recovery environment.
- filesystem check/repair.
- offline logs.
- password/account recovery policy.
- boot configuration repair.
- rollback selection.
- hardware diagnostics.

### Updates
- atomic update staging.
- A/B system slots where adopted.
- signed manifests.
- rollback on failed boot.
- persistent health marker.
- interrupted-update recovery.

---

# PHASE 60 — Release Engineering, Compatibility and RixuriOS 1.0

### Qualification
- boot matrix.
- CPU matrix.
- motherboard/firmware matrix.
- GPU matrix.
- NVMe/SATA/storage matrix.
- NIC matrix.
- USB controller matrix.
- laptop power matrix.
- display/audio matrix.
- QEMU version matrix.

### Release gates
- reproducible release image.
- clean-host rebuild.
- deterministic hashes.
- complete changelog.
- known-issues database.
- security review.
- ABI compatibility review.
- filesystem compatibility review.
- installer/recovery test.
- update/rollback test.
- hardware qualification evidence.
- performance baseline.
- long-duration soak test.

### Final product criteria
RixuriOS 1.0 is not declared complete because it boots or displays a desktop. It requires:

1. stable kernel/user ABI;
2. real process/thread/signal model;
3. functional virtual memory and reclamation;
4. dynamic ELF + TLS + libc runtime;
5. robust VFS and crash-consistent storage;
6. usable IPv4/IPv6 networking and DNS;
7. qualified core hardware drivers;
8. security boundaries tested;
9. recovery and update paths tested;
10. reproducible toolchain and application build;
11. physical-hardware evidence for the supported matrix;
12. documented unsupported hardware and known failures;
13. automated regression coverage;
14. release artifacts that another developer can independently reproduce.

---

# Cross-Phase Engineering Tracks

These tracks run continuously across Phases 00–60 and cannot be postponed to the end.

## A. ABI and Compatibility Track
- syscall ABI versioning;
- structure packing/alignment review;
- userspace ABI tests;
- ELF ABI tests;
- libc compatibility matrix;
- backward-compatibility policy.

## B. Reliability Track
- watchdogs where appropriate;
- timeout ownership;
- recovery state machines;
- leak detection;
- deadlock detection;
- panic/crash classification;
- soak testing.

## C. Security Track
- threat model per subsystem;
- privilege transition review;
- parser fuzzing;
- DMA threat analysis;
- filesystem race analysis;
- syscall attack-surface review;
- supply-chain verification.

## D. Performance Track
- boot time;
- syscall latency;
- context-switch latency;
- scheduler latency;
- page-fault latency;
- block I/O throughput/latency;
- network throughput/latency;
- filesystem throughput;
- graphics frame latency;
- memory overhead.

Performance numbers must always include hardware/QEMU configuration and test method.

## E. Hardware Evidence Track
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

## F. Documentation Track
Every promoted subsystem gets:
- architecture document;
- public ABI/API document;
- error/recovery table;
- diagnostic commands;
- test instructions;
- limitations;
- hardware compatibility notes;
- historical regression notes.

---

# Global Definition of Done

A phase may be marked `COMPLETE` only when all applicable gates below are satisfied:

- [ ] specification reviewed;
- [ ] dependency phases satisfied;
- [ ] implementation exists on the real execution path;
- [ ] build is clean;
- [ ] positive tests pass;
- [ ] negative tests pass;
- [ ] boundary/timeout tests pass;
- [ ] QEMU path passes where applicable;
- [ ] integration path passes;
- [ ] physical hardware tested where applicable;
- [ ] historical regressions pass;
- [ ] security review completed;
- [ ] performance baseline recorded;
- [ ] diagnostics are usable;
- [ ] documentation is updated;
- [ ] unsupported cases are explicit;
- [ ] evidence artifacts are retained;
- [ ] checkpoint ledger is updated.

`SKIP`, `ENOSYS`, printed success messages, source-level stubs, detection-only logs and synthetic hardware events do not satisfy a completion gate unless the roadmap explicitly defines that behavior as the intended supported contract.

# Recommended Dependency Spine

`SMP → preemption → VM/COW → signals → kernel threads/futex → dynamic ELF/TLS → musl → VFS completion → TCP/DNS → service manager → security hardening → hardware qualification → recovery/update → release`

Graphics, audio and desktop applications depend on this spine; they must not be used to mask missing kernel/runtime foundations.
