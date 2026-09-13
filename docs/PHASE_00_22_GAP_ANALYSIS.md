# RixuriOS Phases 00–22 Gap Analysis

**Author:** Manus AI  
**Repository:** `https://github.com/jepdnwp/RixuriOS`  
**Audited revision:** `cf7743218ce1f6442f1952d3721324a5d11c1a0a`  
**Audit date:** 2026-09-12  
**Scope:** Roadmap Phases 00 through 22, inclusive

## Executive finding

RixuriOS has a substantial working vertical slice: a UEFI loader, x86_64 kernel, page tables, exceptions, cooperative processes, static ring-3 programs, NVMe-backed RixFS in QEMU, a shell and utility set, a bounded security model, QEMU E1000 networking, and a native static libc surface. That implementation is materially stronger than a prototype consisting only of stubs. It is nevertheless **not a completed Phase 00–22 platform** under the repository's own evidence rules. The authoritative roadmap requires implementation, clean builds, negative tests, QEMU and physical evidence where applicable, regression coverage, security review, recovery behavior, performance evidence, documentation, and archived checkpoints. It explicitly states that `SKIP`, `ENOSYS`, `NOT TESTED`, `DEGRADED`, `BLOCKED`, `FAIL`, and `UNSUPPORTED` do not mean complete.[1] [2]

The evidence-based overall status is therefore **PARTIAL / HARDENING REQUIRED, with a current-tree build-integrity blocker**. Phase 06 is **BROKEN against its stated preemptive-scheduler requirement** because the current scheduler is cooperative and the timer-preemption attempt was reverted after allocator corruption. Phase 15 is **BROKEN/BLOCKED** because the Makefile and xHCI implementation reference `kernel/usb/xhci_profile.{c,h}` and `tests/xhci_profile_test.c`, but those files are absent from the audited revision. Phase 16 is consequently **BLOCKED** for end-to-end USB input. Phase 22 is the only phase with a documented **PASS for an explicitly bounded static compatibility scope**; that PASS does not include dynamic linking, full pthreads, signal delivery frames, blocking sockets, or physical qualification.[3] [4] [5]

Current execution evidence is unavailable. `gcc`, `x86_64-linux-gnu-gcc`, `x86_64-elf-gcc`, and `qemu-system-x86_64` are absent from the audit environment. Historical results in `docs/VALIDATION_LOG.md` are retained as evidence of earlier environments, but they are not promoted to current-HEAD PASS. The newest validation entry also leaves the full suite and UP/SMP4 sanity pending after later fixes.[3]

> **Evidence rule used in this report:** source presence proves implementation only. A historical host or QEMU log proves only the named revision, topology, path, and assertion. It does not prove the current checkout, physical hardware, untested error paths, sustained concurrency, durability, or recovery.

## Method and status language

This analysis re-read `docs/ROADMAP.md`, `docs/VALIDATION_LOG.md`, `docs/ARCHITECTURE.md`, `docs/CHECKPOINTS.md`, `docs/IMPLEMENTATION_STATUS.md`, the Phase 21 and Phase 22 closure documents, hardware notes, the canonical Makefile, and relevant implementation and test files. The architectural layering remains coherent with the declared UEFI → kernel primitives → memory/interrupts → processes/syscalls → PCI/devices → VFS → libc/TTY/userspace sequence.[6]

| Status | Meaning in this report |
|---|---|
| **PASS (bounded scope)** | The repository has an explicit, narrowly defined closure record and evidence for that scope. Any exclusions remain visible. |
| **PARTIAL** | Useful implementation and some evidence exist, but one or more roadmap requirements or applicable checkpoints remain open. |
| **HARDENING REQUIRED** | The main path exists, but a correctness, security, ownership, concurrency, or recovery defect prevents closure. |
| **BROKEN** | The current implementation contradicts a mandatory requirement or the current tree has a deterministic integration/build defect. |
| **BLOCKED** | Required evidence cannot be obtained with the present environment or hardware. |
| **MISSING** | No corresponding implementation or test evidence was found. |
| **UNVERIFIED** | A claim may be plausible or historically recorded, but no applicable current evidence was found. |

## Phase-by-phase analysis

## Phase 00 — Governance and Reproducible Build

### Roadmap requirement

The roadmap requires a canonical generated-file policy, pinned host and cross toolchains, deterministic compiler/linker/image generation, debug and release profiles, provenance, continuous integration, artifact retention, a checkpoint ledger, ABI/versioning, and release-blocker policy.[1]

### Current implementation

The Makefile defines freestanding C17, `-Wall -Wextra -Werror`, explicit linker checks, UEFI image and ISO targets, host-test targets, QEMU targets, and a unified test runner. `docs/CHECKPOINTS.md` defines CP0–CP8 and evidence rules. The linker script separates `R-X`, `R--`, and `RW-` segments and declares a non-executable GNU stack.[2] [4] [7]

### Status

**PARTIAL / HARDENING REQUIRED / CURRENT BUILD BLOCKED.**

### Implemented

A canonical build entry point, strict warning policy, generated build directory, image/ISO construction, host tests, QEMU scripts, checkpoint definitions, and historical validation ledger exist. Historical logs record strict builds and image/ISO generation in earlier environments.[3] [4]

### Missing

Pinned compiler/binutils/UEFI/mtools/xorriso/QEMU versions, hermetic bootstrap, CI configuration, debug versus release profiles, retained immutable artifacts, machine-readable provenance, reproducible image normalization, release signing, and a formal release-blocker workflow are not evidenced. No CI workflow exists in the audited tree.

### Partial

Build provenance is embedded in `build/build_id.h`, but it includes wall-clock time and dirty state on every invocation. This makes byte-identical release reproduction impossible without a separate deterministic mode. Generated evidence under `build/` is ignored and not retained.[4] [8]

### Bugs / correctness risks

The Makefile links `kernel/usb/xhci_profile.o` and builds `xhci-profile-test`, but the referenced source, header, and test files are absent at audited HEAD. A clean build must therefore fail once a compiler is available. `scripts/run-all-tests.sh` currently uses `set -uo pipefail`, while a historical audit states it was changed to `set -euo pipefail`; this documentation/source drift weakens confidence in the ledger.[3] [4] [9]

### Tests

| Class | Evidence | Result |
|---|---|---|
| Host | `make test` aggregates parser and subsystem tests. | Historical PASS; **current UNVERIFIED/BLOCKED**. |
| Unit | USB, HID, TTY, shell, pipe, network, libc, ACPI, SMP, GDT, xHCI capability, and RixFS mount tests are wired. | Incomplete graph; absent profile test breaks current source contract. |
| Integration | `image`, `iso`, `iso-test`, `test-all`. | Historical evidence only. |
| QEMU | Numerous `scripts/qemu_*_test.py` harnesses. | Historical bounded evidence; QEMU unavailable now. |
| Physical | No reproducible build-to-physical artifact chain was found. | **UNVERIFIED**. |

### Validation gaps

No fresh clean build, no two-build reproducibility comparison, no SBOM/provenance manifest, no CI artifact archive, no test-skip enforcement, and no current-HEAD QEMU run exist. The toolchain and QEMU are absent from the audit environment.

### Required work

Restore the missing xHCI profile files or remove all references consistently. Pin and bootstrap the full toolchain. Add CI with clean checkout builds, test matrices, artifact hashes, serial logs, and retention. Add a reproducible release mode based on `SOURCE_DATE_EPOCH`, normalized filesystems and stable ordering. Make absent applicable tests fail or be explicitly BLOCKED instead of succeeding through `SKIPPED`.

### Exit criteria

Two isolated clean builds of the same revision produce byte-identical kernel, ESP, disk image, and ISO; CI records exact tool versions and hashes; `make clean all check test image iso iso-test test-all` is green; no required test is silently skipped; artifacts and logs are retained; release blockers and ABI versions are recorded.

## Phase 01 — UEFI Boot and Firmware Handoff

### Roadmap requirement

Implement the EFI ABI, ELF64 loading, PT_LOAD allocation/copy/zero-fill, ACPI RSDP and GOP discovery, memory-map capture, `ExitBootServices()` retry, diagnostics, and firmware-quirk handling.[1]

### Current implementation

`boot/efi_main.c` validates ELF64 class/data/machine/header dimensions, bounds program headers, allocates the aggregate load span, zeroes it, copies PT_LOAD contents, captures ACPI and GOP, obtains the memory map with slack and four growth retries, retries `ExitBootServices()` once, and enters the kernel using the System V ABI.[10]

### Status

**PARTIAL.**

### Implemented

The core UEFI path and fixed versioned handoff are real. Historical QEMU/OVMF logs reach kernel initialization and ring 3. The loader frees the file buffer after parsing and frees failed memory-map buffers during growth retries.[3] [10]

### Missing

A mock EFI Boot Services test harness, malformed-loader corpus, EBS fault injection, removable-media firmware matrix, GOP format/stride validation tests, allocation rollback for all loader failures, and physical UEFI boot evidence are missing.

### Partial

The ELF loader checks that the entry is within the aggregate PT_LOAD span, but it does not prove the entry lies in an executable PT_LOAD. It does not reject overlapping PT_LOAD memory ranges. Handoff C-side validation exists, but the assembly entry copies 13 qwords before that validation; safety therefore depends on the loader as the sole trusted producer.[10] [11]

### Bugs / correctness risks

Malformed overlapping segments can produce ambiguous copied contents. An entry in a non-executable segment can pass the aggregate range check. Firmware map growth after the single EBS retry fails the boot rather than entering a broader bounded retry loop. GOP metadata is accepted without a complete pixel-format and framebuffer-bound contract.

### Tests

| Class | Evidence | Result |
|---|---|---|
| Host | No loader/Boot Services mock test. | **MISSING**. |
| Unit | No malformed ELF/EBS/map suite for `boot/efi_main.c`. | **MISSING**. |
| Integration | Loader → handoff → kernel is present. | Historical QEMU only. |
| QEMU | Historical UEFI image and ISO reach kernel/shell markers. | **PASS for bounded historical QEMU boot only**. |
| Physical | No removable-media boot log tied to audited artifacts. | **UNVERIFIED/BLOCKED**. |

### Validation gaps

Bad ELF class/machine/header sizes, integer overflow, overlap, entry permissions, allocation failures, changing map keys, repeated EBS invalid-parameter responses, absent RSDP/GOP, framebuffer anomalies, and firmware quirks are not systematically injected.

### Required work

Factor loader logic behind mockable EFI service interfaces. Add deterministic positive and negative host tests, leak checks, executable-entry and non-overlap rules, robust bounded EBS retries, GOP validation, and archived QEMU plus physical firmware logs.

### Exit criteria

All malformed inputs fail closed with no leaked pages or pools; an EBS map-growth/retry trace is tested; removable `BOOTX64.EFI` boots the exact archived artifact in QEMU and on the ASUS target; handoff version/size and framebuffer bounds are validated before use.

## Phase 02 — CPU, PMM, VMM and Kernel Memory

### Roadmap requirement

Provide CPU feature policy, PMM ownership, paging and permission transitions, DMA-capable allocation, page-fault handling, a reclaiming kernel heap, and leak/corruption diagnostics. The explicit gate is that kernel memory is genuinely reclaimable.[1]

### Current implementation

PMM parses UEFI descriptors and tracks managed, used, and reserved pages. VMM constructs early identity mappings, maps/unmaps 4 KiB pages, splits 2 MiB leaves, maps uncached MMIO, translates addresses, walks arbitrary roots, validates page tables recursively, and controls CR0/CR4/EFER. Address spaces own user pages. Page-fault diagnostics are detailed.[12] [13] [14]

### Status

**HARDENING REQUIRED; reclaimability gate not met.**

### Implemented

Descriptor bounds, frame allocation, contiguous allocation, reserved-page API, mapping flags, NX, CR0.WP, LA57 normalization, MMIO PWT/PCD, CR3 validation, local invalidation, and SMP shootdown hooks exist. The linker records segment permissions.[7] [12] [13]

### Missing

A general reclaiming kernel allocator, page-table reclamation, a centralized DMA mapping/pinning/scatter-gather contract, allocator poisoning/quarantine, leak accounting, guard pages, and comprehensive PMM/VMM tests are missing.

### Partial

`pmm_init()` marks kernel, boot metadata, and map pages used, but does not set their `reserved_bitmap` bits. `pmm_free_page()` rejects only unmanaged, already-free, or explicitly reserved pages. A bad caller can therefore return live kernel or boot metadata frames. Runtime W^X remains incomplete because the early identity map marks the entire mapped physical range writable with huge pages; linker PHDRs alone do not enforce page permissions.[7] [12] [13]

### Bugs / correctness risks

`kfree()` is a no-op. `kmalloc()` obtains a new page before discovering that an oversized/alignment-constrained allocation cannot fit, leaking that page. PMM has no visible synchronization. Page-map mutations lack a general concurrent mapper protocol. A broad writable identity map permits writes to code unless a later mapping policy removes that permission. Faults in kernel uaccess fail-stop the CPU rather than recovering.[12] [13] [15]

### Tests

| Class | Evidence | Result |
|---|---|---|
| Host | No PMM, VMM, heap, or address-space target in `make test`. | **MISSING**. |
| Unit | No double-free, reserved-free, exhaustion, map/unmap, W^X, rollback, or leak suite. | **MISSING**. |
| Integration | Ring-3 and process paths exercise allocations indirectly. | Partial, not memory qualification. |
| QEMU | Historical ring-3 boot and fault diagnostics. | Smoke only; targeted matrix **MISSING**. |
| Physical | No memory-map diversity, pressure, or fault evidence. | **UNVERIFIED**. |

### Validation gaps

No free-page-count stability test, descriptor fuzzing, allocation exhaustion, reserved-frame rejection, huge-page split rollback, empty page-table reclaim, write-to-text/execute-data test, supervisor/user isolation matrix, concurrent mapping test, or long-duration memory-pressure run exists.

### Required work

Define frame ownership and lifetime. Reserve immutable boot/kernel/framebuffer/page-table ranges permanently. Replace the monotonic heap with a reclaiming allocator. Add lock/preemption rules. Apply final kernel segment permissions. Build host models plus QEMU fault/pressure tests and a central DMA mapping API.

### Exit criteria

Repeated allocation/free returns to a stable page count; reserved frames cannot be freed; heap objects are reused safely; page-table allocations are reclaimed; text is read/execute and non-writable; data is writable/NX; user mappings cannot access supervisor pages; all failures roll back without leaks on UP and SMP4.

## Phase 03 — GDT/TSS/IDT/Exceptions/Interrupts

### Roadmap requirement

Implement GDT, TSS and IST, a stable trap-frame ABI, exceptions 0–31, IRQ entry/EOI/nesting, PIC compatibility, spurious IRQ handling, and nested-fault/malformed-return tests.[1]

### Current implementation

GDT/TSS construction and load paths exist. The IDT installs exceptions 0–31, IRQs 32–47, IPIs 224–226, and DPL3 syscall vector `0x80`. Exception gates use IST1. Assembly stubs distinguish error-code vectors. IRQ dispatch sends EOI. Fault diagnostics record CR2/CR3 and page-table state, then halt.[16] [17]

### Status

**PARTIAL / HARDENING REQUIRED.**

### Implemented

Descriptor builders, per-CPU TSS registration for APs, exception/IRQ/syscall/IPI gates, saved-register assembly entry, page-fault diagnostics, nested-fault guards, and historical QEMU interrupt initialization evidence exist. `gdt_test` covers pure builder and registry behavior.[3] [16]

### Missing

Privileged exception-frame tests, all-vector injection, malformed `iretq` frames, IRQ nesting policy, spurious PIC/APIC regressions, exception recovery policy, stack guard pages, and physical interrupt-routing evidence are missing.

### Partial

The implementation intentionally fail-stops after any exception. That is acceptable for kernel-fatal faults, but it cannot support recoverable user faults or fault-safe uaccess without a fixup or process-kill path. Historical fixes for error-code stack cleanup are documented, but there is no current QEMU regression proving every error/no-error vector shape.[16] [18]

### Bugs / correctness risks

Forensic reads of a corrupt stack or RIP can generate a nested fault and halt. All exceptions use IST1, which simplifies diagnostics but needs documented nesting and stack-capacity guarantees. Physical APs require validated per-CPU GDT/TSS/IST behavior; virtual evidence does not prove it.

### Tests

| Class | Evidence | Result |
|---|---|---|
| Host | `tests/gdt_test.c`; some SMP descriptor checks. | Historical PASS; current blocked. |
| Unit | Pure descriptor construction only. | Partial. |
| Integration | IDT/IRQ/PIT used in boot. | Historical smoke. |
| QEMU | Boot and prior fault forensics exist. | No complete vector/malformed-return matrix. |
| Physical | No archived exception/IRQ result. | **UNVERIFIED**. |

### Validation gaps

No deterministic proof of frame layout for all exceptions, no nested page-fault/double-fault test, no spurious IRQ test, no malformed user-return test, no EOI ordering test, and no physical PIT/IOAPIC interrupt evidence.

### Required work

Create fault-injection entry points and a QEMU vector matrix. Add host ABI assertions for frame encoding, privileged tests for error-code cleanup and return-state validation, process-scoped user-fault recovery, double-fault IST exhaustion checks, spurious IRQ coverage, and physical serial captures.

### Exit criteria

Every vector has a documented and tested frame shape; invalid user return state is rejected before `iretq`; user faults terminate or signal only the process; kernel nested faults fail safely with retained diagnostics; IRQ nesting/EOI behavior passes QEMU and target-hardware tests.

## Phase 04 — ACPI/LAPIC/IOAPIC/Timers/SMP

### Roadmap requirement

Validate ACPI tables, parse MADT and interrupt overrides, support LAPIC/x2APIC, start APs with per-CPU state, route IPIs, provide APIC timer/HPET/PIT behavior, and perform TLB shootdown. Multiple CPUs must execute safely.[1]

### Current implementation

ACPI parses RSDP/XSDT/RSDT, checks SDT signatures/checksums/lengths, and extracts MADT CPUs, x2APIC entries, IOAPICs, overrides, MCFG, and FADT/S5 data. LAPIC/x2APIC access, INIT/SIPI/fixed IPIs, IOAPIC routing, PIT ticks, AP startup, per-AP stacks/GDT/TSS/DF stacks, ping, wakeup, shootdown, and restricted AP kernel-thread execution exist.[19] [20]

### Status

**PARTIAL.**

### Implemented

Historical WHPX SMP4 logs record AP1–3 online, ping success, shootdown success, `online=4`, AP probe execution, shell readiness, and zero exception/panic/timeout markers. Host ACPI/SMP/GDT tests cover synthetic table and protocol behavior.[3]

### Missing

APIC timer or HPET implementation/evidence, physical topology and x2APIC qualification, supported behavior above eight CPUs, AP offline/restart, resource cleanup after partial startup, stack guards, and sustained SMP workload testing are missing.

### Partial

Production workers and user tasks remain mostly BSP-bound. The scheduler has per-CPU current slots but not full per-CPU runqueues. ACPI firmware pointers are directly dereferenced after table checks without a general physical-access/range-validation layer. Timer support exercised in the tree is PIT-only.[19] [20]

### Bugs / correctness risks

`lapic_id()` shifts the MMIO ID register and can truncate x2APIC IDs above 255 despite 32-bit topology storage. Startup allocates trampoline/stack/TSS/DF resources with incomplete rollback on later failure. The code formally defers larger topologies, while the Ryzen target reports 16 logical CPUs. Fixed spin delays can diverge sharply between nested virtualization and hardware.[20] [21]

### Tests

| Class | Evidence | Result |
|---|---|---|
| Host | `acpi_test`, `smp_test`, `gdt_test`. | Historical PASS; current blocked. |
| Unit | Synthetic ACPI and mocked startup/IPI/shootdown contracts. | Substantial but non-privileged. |
| Integration | Scheduler AP probe and TLB hook. | Historical WHPX only. |
| QEMU | UP and WHPX SMP4 historical PASS on an earlier tree. | Current revalidation **PENDING/UNVERIFIED**. |
| Physical | No Ryzen topology/APIC/timer evidence. | **BLOCKED**. |

### Validation gaps

No malformed physical-pointer ACPI fuzzing, x2APIC ID >255 case, duplicate/disabled CPU policy, multiple IOAPIC matrix, APIC-timer calibration, AP partial-failure recovery, CPU-count boundary, cold/warm boot repetition, or physical TLB-shootdown workload exists.

### Required work

Harden firmware physical-range access and ACPI arithmetic. Preserve full x2APIC IDs. Implement or explicitly defer APIC timer/HPET. Define the supported CPU-count policy, reclaim failed AP resources, add online/offline state transitions, and rerun UP/SMP4 plus the 16-thread target with retained logs.

### Exit criteria

Current HEAD passes UP and SMP4 QEMU repeatedly; the supported physical topology boots repeatedly; every online CPU runs a probe; IPI and shootdown results are observed; timer calibration and tick bounds are measured; malformed ACPI fails closed; partial AP failure degrades without leaks or misrouting.

## Phase 05 — Synchronization and Kernel Workers

### Roadmap requirement

Provide spinlocks, IRQ-save locks, mutexes, reader/writer locks, semaphores, wait queues, reference counting, lock-order/deadlock diagnostics, and cancellable kernel workers.[1]

### Current implementation

Spin, try, IRQ-save, and IRQ-restore locks exist. A bounded waiter-state container supports prepare, mark-blocked, wake-one, wake-all, and removal. Kernel threads and selected polling workers exist. SMP work documents deliberate one-worker-at-a-time migration and subsystem-specific locking audits.[21] [22]

### Status

**PARTIAL / HARDENING REQUIRED.**

### Implemented

Low-level spin primitives, some subsystem locks, scheduler lock, console/input serialization, task-context PS/2 processing, and a finite waiter registry exist.

### Missing

Kernel mutexes, RW locks, semaphores, generic refcounts, lockdep/order checking, priority inheritance, scheduler-integrated blocking, timeout/cancellation, worker join/drain lifecycle, and global deadlock diagnostics are missing.

### Partial

The wait queue changes only waiter metadata. It does not atomically bind a condition to enqueueing, transition a scheduler task to BLOCKED, wake a specific task, or handle timeout/signal/cancellation. Production pipes, wait, nanosleep, and sockets therefore yield or return rather than use a complete sleep/wakeup primitive.[22]

### Bugs / correctness risks

The wait-queue lock is a plain spinlock rather than an IRQ-save contract. Introducing interrupt users could deadlock. Condition checks outside an atomic enqueue protocol can lose wakeups. Enabling broader AP worker migration before subsystem locking is complete can race VFS, networking, page tables, and device rings.[21] [22]

### Tests

| Class | Evidence | Result |
|---|---|---|
| Host | Pipe and SMP tests touch some locking paths. | Indirect only. |
| Unit | No dedicated lock/waitqueue/refcount/deadlock target. | **MISSING**. |
| Integration | Selected workers execute in historical SMP logs. | Partial. |
| QEMU | AP probe and migrated serial worker were observed historically. | Not a synchronization stress test. |
| Physical | No lock/worker stress evidence. | **UNVERIFIED**. |

### Validation gaps

No owner violation, IRQ nesting, lost-wakeup, saturation, timeout, cancellation, lock-order inversion, producer/consumer, or SMP lock-contention tests exist.

### Required work

Define scheduler blocking states and IRQ-context rules. Implement real wait queues, mutex/RW/semaphore/refcount APIs, cancellation and worker teardown. Add lockdep-lite ownership/order diagnostics. Migrate workers only after per-subsystem audits.

### Exit criteria

Host tests prove lock ownership and ordering; wait conditions cannot lose wakeups; pipe/wait/sleep/socket users block and wake correctly; cancelled workers release resources; SMP4 stress completes without deadlock, starvation, timeout, or lock-order warning.

## Phase 06 — Processes, Threads and Preemptive Scheduler

### Roadmap requirement

Implement PID/TID lifecycles, process and thread objects, kernel and user threads, timer preemption, per-CPU queues, SMP balancing/affinity, and scheduler accounting.[1]

### Current implementation

The kernel has PID-indexed process objects, fork/exec/exit/wait, independent address spaces, fixed scheduler task slots, kernel threads, user contexts, context switching, per-CPU current slots, a scheduler lock, AP idle contexts, and opt-in AP kernel-thread affinity.[21] [23]

### Status

**BROKEN against the mandatory preemptive-scheduler gate; otherwise PARTIAL.**

### Implemented

Cooperative task creation/switching, fork context restoration, process activation, zombie states, fixed-size task accounting, and restricted AP kernel-thread execution are present. Historical QEMU demonstrates process/shell behavior and bounded AP work.

### Missing

TIDs and user-thread objects, shared-address-space user threads, scheduler-integrated blocked state, timer preemption, per-CPU runqueues, load balancing, priorities, quantum accounting, general affinity, robust task references, and scalable task capacity are missing.

### Partial

The scheduler is explicitly cooperative: `scheduler_tick()` increments a counter and `scheduler_yield()` performs selection. APs run only selected kernel threads. Process exit marks a zombie and closes FDs, while task retirement and process resource ownership are not represented by a unified reference protocol.[23] [24]

### Bugs / correctness risks

The prior timer-preemption attempt was reverted because IRQ-context yielding exposed unlocked PMM, heap, VMM, and process operations to corruption and hangs. A non-yielding task can starve the system. `sched_cpu()` falls back to the BSP index and then zero when CPU identity fails, potentially aliasing AP state. Fixed DEAD-slot reuse can race stale task/process references. `process_logout_session()` can mark sibling processes exited without explicit scheduler-task retirement.[3] [23] [24]

### Tests

| Class | Evidence | Result |
|---|---|---|
| Host | `smp_test` tests startup/IPI plumbing, not scheduler selection. | No scheduler harness. |
| Unit | No context-switch, runqueue, preemption, task-lifetime, or timer-queue target. | **MISSING**. |
| Integration | Fork/exec/wait and AP probe paths exist. | Historical bounded QEMU. |
| QEMU | Process utilities and SMP boot/probe have historical evidence. | No preemptive or scheduler stress PASS. |
| Physical | No preemptive SMP evidence. | **UNVERIFIED/BLOCKED**. |

### Validation gaps

No never-yielding hog test, IRQ-preempted allocator test, task-table exhaustion/reuse test, migration test, fairness measurement, process-exit versus scheduler race, user-thread test, or UP/SMP long soak exists.

### Required work

First complete Phase 05 blocking and locking. Introduce preempt-disable nesting and audit every shared kernel subsystem. Add thread/TID objects with refcounted process and address-space ownership. Implement per-CPU runqueues, timer or reschedule IPI, accounting, affinity and migration. Then reintroduce preemption in staged gates.

### Exit criteria

A non-yielding task is preempted on UP and SMP4; user and kernel threads block/wake and exit safely; repeated fork/exec/exit at slot limits has no stale task; allocator/VFS/IPC/device stress survives IRQ preemption; fairness and latency are measured; no corruption, fault, starvation, or hang occurs across repeated runs.

## Phase 07 — Syscall ABI and Uaccess

### Roadmap requirement

Provide stable numbering/versioning, entry and return ABI, canonical/range validation, fault-safe copyin/copyout, FD/object validation, errno/restart policy, and ABI fuzzing.[1]

### Current implementation

`int 0x80` entry and register-frame dispatch exist. Syscall IDs and `RIX_SYSCALL_ABI_VERSION` are defined. `user_range_valid()` checks canonical user ranges and PTE user/write permissions. Many syscall cases validate pointers through `copy_from_user()` or `copy_to_user()` and return negative errno-style values.[15] [25]

### Status

**PARTIAL / HARDENING REQUIRED.**

### Implemented

A stable-looking version-1 table, ring-3 entry, broad process/VFS/IPC/time/network/security calls, canonical and overflow checks, and selected negative guest probes exist.

### Missing

A fault-recovery/fixup mechanism, concurrent-unmap safety, complete per-call pointer/size/FD/object policy, restart semantics, cancellation points, ABI compatibility policy, generated ABI documentation, and systematic fuzzing are missing.

### Partial

Uaccess validates mappings and then directly dereferences user memory. A mapping change or latent fault between validation and access enters the fail-stop exception path. Error mapping is inconsistent: some copy failures, including clock/nanosleep paths, become `EINVAL` instead of precise `EFAULT`. `openat` accepts a dirfd but current dispatch ignores its path-base semantics.[15] [25]

### Bugs / correctness risks

A malicious or racing user process can turn a bad pointer into a kernel halt. Raw frame and size combinations are not fuzzed. Incomplete errno distinctions break callers and hide authorization/path bugs. The provisional Phase 19 syscall design collides at IDs 137–139 with existing capability/process/random assignments.[26]

### Tests

| Class | Evidence | Result |
|---|---|---|
| Host | No uaccess/syscall-dispatch target. | **MISSING**. |
| Unit | No pointer race, frame, FD, restart, or ABI-number collision suite. | **MISSING**. |
| Integration | `abi-negative`, POSIX, shell, process, and credential programs use the ABI. | Partial. |
| QEMU | Historical ring-3 and selected malformed-pointer evidence. | No fuzzing or concurrent-unmap proof. |
| Physical | No syscall/fault evidence. | **UNVERIFIED**. |

### Validation gaps

Unmapped, kernel, noncanonical, cross-page, read-only, concurrently unmapped, overflowed, stale-FD, wrong-object, interrupted, and malformed-frame cases are not exhaustively exercised.

### Required work

Implement exception-table/fixup or an equivalent guarded uaccess mechanism. Freeze a versioned syscall-number registry and resolve collisions. Specify exact errno and restart behavior. Generate syscall metadata and fuzz every pointer, length, flags, FD, and object type through the real ring-3 entry path.

### Exit criteria

Invalid user inputs return documented errors without halting; concurrent unmap cannot crash copyin/out; unknown calls return `ENOSYS`; IDs are unique and versioned; each syscall has positive, boundary, negative, privilege, interruption, and exhaustion coverage on UP and SMP4.

## Phase 08 — User VM and ELF64 Execution

### Roadmap requirement

Provide independent address spaces, strict kernel/user permissions, validated ELF64 loading, BSS, an aligned `argc`/`argv`/`envp`/`auxv` stack, exec replacement/teardown, and user-fault isolation.[1]

### Current implementation

`kernel/process/address_space.c` creates/destroys PML4 roots, borrows kernel slots, maps owned/shared user pages, clones spaces, and synchronizes kernel slots. `kernel/elf/elf.c` validates ELF64 and PT_LOAD bounds/alignment, `filesz <= memsz`, executable entry, BSS, and W^X. Process code connects fork/exec to independent roots. Static ring-3 programs run in historical QEMU.[3] [27] [28]

### Status

**PARTIAL.**

### Implemented

Independent CR3 roots, owned user mappings, full-copy clone, teardown, static loading, BSS zeroing, W^X flags, initial user stacks, argument/environment vectors, fork, and exec exist.

### Missing

An explicit ET_EXEC/ET_DYN/PIE/PT_INTERP policy, relocations, ASLR, guarded stacks, copy-on-write, demand paging, atomic low-level mapping transactions, process-local page-fault recovery, and dedicated VM/ELF host tests are missing.

### Partial

Dynamic ELF is explicitly deferred to Phase 23. Mapping failures rely on caller-level replacement teardown. Historical shell execution proves bounded static integration, not concurrent VM stress or fault isolation.[5] [28]

### Bugs / correctness risks

Overlapping PT_LOAD policy is not comprehensively tested. Concurrent page-table mutation lacks a general synchronization contract. The historical `proc-test` → `pipe-stress` vector-6 fault remains an unresolved task/page-table/context-lifetime regression.[3]

### Tests

| Class | Evidence | Result |
|---|---|---|
| Host | No VMM/address-space/ELF target. | **MISSING**. |
| Unit | No overlap, rollback, stack, clone/destroy, permission, or fault suite. | **MISSING**. |
| Integration | Static fork/exec/VFS flows. | Historical bounded PASS. |
| QEMU | `qemu_ring3_test.py` and utility suites. | Static smoke/integration only. |
| Physical | No independent-process Ring-3 evidence. | **UNVERIFIED**. |

### Validation gaps

Malformed/overlapping segments, ELF types, BSS boundaries, maximum vectors, stack guards, repeated exec rollback, fork isolation, use-after-unmap, CR3/TLB races, and user-fault containment are not closed.

### Required work

Add host page-table and ELF models; enforce type/overlap/entry policy; make exec transactional; add stack guards and exact stack-layout tests; test clone/destroy ownership and TLB behavior; isolate user faults. Keep the static-only boundary explicit until Phase 23.

### Exit criteria

Two independent processes run concurrently; fork/exec/exit/reap loops are leak-free; malformed ELFs leave the old image intact; permissions and BSS are correct; bad user access affects only that process; UP/SMP4 runs show no stale or cross-process translation.

## Phase 09 — IPC and Process Communication

### Roadmap requirement

Implement pipes/FIFOs, events/wait objects, shared memory, process groups, signal architecture, Unix-domain sockets/FD passing, and process-death cleanup.[1]

### Current implementation

A 4096-byte channel backs pipes. VFS allocates endpoints and retains them across dup/fork. Shared-memory create/map/unmap/destroy functions exist. Process groups and pending/masked signals exist. Historical QEMU covers pipes, fork/wait, bounded reader blocking, and control-signal prompt recovery.[3] [29]

### Status

**PARTIAL / HARDENING REQUIRED.**

### Implemented

Bounded pipes, EOF/closed checks, endpoint references, shared-memory mappings, process groups, pending masks, group signaling, and shell-pipeline integration exist.

### Missing

Named FIFOs, scheduler-backed events, Unix sockets, FD passing, full signal frames, reliable death cleanup, shared-memory authorization, and blocking writer semantics are missing.

### Partial

Reads and waits yield/poll. A full channel produces partial/error behavior; syscall write does not implement a complete wait/retry contract. Shared-memory refs are decremented only by explicit unmap, not demonstrated death/exec teardown.[24] [29]

### Bugs / correctness risks

Large writes can stall pipelines or lose clear semantics. Endpoint leaks suppress EOF. Process exit may leak shared mappings. Adding blocking without atomic condition/enqueue would introduce lost wakeups.

### Tests

| Class | Evidence | Result |
|---|---|---|
| Host | `pipe_test` covers bounded I/O, EOF, closed-reader rejection. | Historical PASS; current blocked. |
| Unit | No SHM lifetime, event, Unix-socket, FD-passing, or death suite. | **MISSING**. |
| Integration | Real shell pipelines and fork/wait. | Partial. |
| QEMU | Eight standalone pipe rounds and signal prompt recovery. | Partial; full writer and cross-test fault remain. |
| Physical | No IPC-specific evidence. | **UNVERIFIED**. |

### Validation gaps

Multi-reader/writer fairness, backpressure, close races, EPIPE, interruption, exhaustion, SHM permission/lifetime, crash cleanup, FD passing, and SMP contention are untested.

### Required work

Complete wait queues and task lifecycle first. Add blocking/nonblocking pipe semantics, event objects, SHM ownership/refcounts, Unix sockets, FD passing, and exit cleanup.

### Exit criteria

Pipelines exceed channel capacity without loss/deadlock; close wakes both sides with correct EOF/EPIPE; death releases endpoints/mappings; waits handle timeout/signal/cancel; Unix FD passing is safe; UP/SMP4 races pass.

## Phase 10 — PCIe, MCFG, MMIO and DMA

### Roadmap requirement

Implement PCI/ECAM discovery, capability parsing, BAR/MMIO ownership, DMA mapping/cache rules, MSI/MSI-X, driver lifecycle, bridge/multifunction handling, and IOMMU abstraction.[1]

### Current implementation

PCI discovery, capability traversal, BAR sizing, device binding, page-list DMA allocation, uncached MMIO mapping, and MSI-X helpers exist. ACPI extracts MCFG. `iommu_init()` deliberately reports unavailable and map/unmap fail closed.[30] [31]

### Status

**PARTIAL / HARDENING REQUIRED.**

### Implemented

Inventory, class matching, capability walks, BAR decoding/sizing, driver bus-master paths, MMIO mapping, page allocation, MSI-X register helpers, and binding primitives exist.

### Missing

BAR ownership/release, fully qualified bridges, hot removal, reset cleanup, DMA pin/map/unmap, cache/barrier rules, bounce/SG support, MSI-X lifecycle, and DMAR/IVRS domains are missing.

### Partial

DMA returns physical pages, not device-domain mappings with ownership. Drivers embed separate DMA assumptions. MMIO uses PWT/PCD but memory-attribute aliasing/lifetime is not generally managed.[13] [30]

### Bugs / correctness risks

Without IOMMU, a bad device/driver can DMA outside intended buffers. Fragmented buffers are not generally represented. Reset can leave live DMA. MSI-X target/lifecycle is not qualified on SMP hardware.

### Tests

| Class | Evidence | Result |
|---|---|---|
| Host | E1000/RTL8125 models and xHCI capability parser. | Indirect/partial. |
| Unit | No config/BAR/ownership/DMA/IOMMU/MSI-X suite. | **MISSING**. |
| Integration | QEMU NVMe and E1000 use PCI/MMIO/DMA. | Historical bounded PASS. |
| QEMU | Selected virtual devices. | Partial. |
| Physical | Inventory exists; operation is not proved. | **UNVERIFIED/BLOCKED**. |

### Validation gaps

Capability loops, BAR overlap/overflow, bridges, multifunction, 64-bit BAR edges, MSI-X mask/pending, DMA-after-free, fragmentation, IOMMU on/off, reset teardown, and SMP targeting are open.

### Required work

Create central resource and DMA-domain APIs with pinning, SG, barriers, coherency, ownership, reset teardown, and IOMMU domains or a constrained no-IOMMU mode. Add fake config/DMA tests, then QEMU and physical validation.

### Exit criteria

Resources cannot overlap/outlive devices; malformed capabilities fail closed; every DMA buffer has owner, direction, lifetime, and translation; DMA-after-free is rejected; MSI-X teardown passes UP/SMP4; physical PCI IDs/status are archived.

## Phase 11 — Storage Core

### Roadmap requirement

Provide block registry, BIO/request/SG, queue ordering, flush/FUA/barriers, timeout/retry/reset, cache/writeback, and persistent error states.[1]

### Current implementation

`block_submit()` validates bounds, counts, byte arithmetic, and flush shapes before synchronous dispatch. A locked 64-entry write-back cache supports read/write/flush. NVMe registers namespaces as block devices.[32] [33]

### Status

**PARTIAL / HARDENING REQUIRED.**

### Implemented

Block registration, bounded BIO validation, synchronous submission, dirty cache entries, age-based victim selection, per-device flush, and RixFS integration exist.

### Missing

Request queues, asynchronous completion, SG, ordering domains, FUA/barriers, generic retry/reset, queue-full policy, terminal error reporting, performance metrics, and a fake-backend suite are missing.

### Partial

Cache metadata is locked while backing I/O runs outside the lock. Internal `io()` bypasses `block_submit()` validation. Flush failure redirties a matching entry, but dirty eviction invalidates the victim before writeback succeeds.[33]

### Bugs / correctness risks

Failed dirty eviction discards the only cached dirty copy. Its temporary copy length uses the incoming device's sector size even when the victim belongs to another device. Concurrent victim/fill decisions can race. Journaling lacks an explicit persistence-order contract.[33]

### Tests

| Class | Evidence | Result |
|---|---|---|
| Host | No block/cache target. | **MISSING**. |
| Unit | No fake backend for range, short I/O, timeout, flush failure, ordering, or recovery. | **MISSING**. |
| Integration | QEMU NVMe → cache → RixFS → VFS. | Historical functional evidence. |
| QEMU | Disposable-image file operations. | Not durability/fault evidence. |
| Physical | No storage-core stress. | **UNVERIFIED**. |

### Validation gaps

Dirty-eviction failure, sector-size mixing, collisions, concurrency, flush/FUA ordering, timeout/reset, partial completion, removal, full disk, and SMP contention are untested.

### Required work

Define request ownership/order. Route cache I/O through validated submission. Preserve dirty data on failure. Implement queues, SG, barriers/FUA, retry/reset, stable error states, a fault-injecting RAM backend, and QEMU stress.

### Exit criteria

Injected errors never lose dirty data; ordering satisfies RixFS; timeouts recover or become stable errors; mixed devices remain isolated; UP/SMP4 workloads finish without corruption, leak, deadlock, or reorder.

## Phase 12 — NVMe

### Roadmap requirement

Implement reset/enable, admin and I/O queues, Identify, PRP/SGL, polling and interrupts, read/write/flush, timeout/reset recovery, and physical-device evidence.[1]

### Current implementation

`nvme.c` discovers controllers, maps BARs, resets/enables, creates admin/I/O queues, identifies controllers/namespaces, registers block devices, and performs synchronous polling read/write/flush. Historical disposable QEMU images mounted NVMe and ran real file operations.[3] [34]

### Status

**PARTIAL; physical and recovery gates BLOCKED.**

### Implemented

Readiness checks, admin setup, Identify, queue creation, namespace geometry, bounded PRP1/PRP2, polling completions, read/write/flush, and block registration exist.

### Missing

General PRP lists/SGL, multiple outstanding commands, interrupt completions, robust stale-CID handling, runtime reset/re-identify/re-register, queue recreation, quiesce, physical I/O evidence, and performance/concurrency testing are missing.

### Partial

`command_prp()` accepts only a first partial page plus one additional page. Timeout returns an error while leaving the controller alive; this avoids a previous global wedge but is not recovery.[3] [34]

### Bugs / correctness risks

Buffers beyond two pages fail. Fragmented backing is unsupported. Timed-out commands may later become stale completions. Runtime controller recovery is absent. Physical DMA/cache/IOMMU assumptions are unqualified.

### Tests

| Class | Evidence | Result |
|---|---|---|
| Host | No queue/PRP/completion target. | **MISSING**. |
| Unit | No phase/CID/wrap/status/timeout/reset model. | **MISSING**. |
| Integration | NVMe → RixFS → shell/utilities. | Historical bounded PASS. |
| QEMU | Identify/mount/read/write; flush-timeout regression fixed. | No reset-recovery matrix. |
| Physical | No safe read/write/flush qualification. | **UNVERIFIED/BLOCKED**. |

### Validation gaps

PRP boundaries, fragmentation, queue wrap, wrong CID/phase, fatal status, timeout/recovery, repeated reset, flush durability, concurrency, 4 KiB LBA, full media, and physical status codes are open.

### Required work

Implement PRP lists or SGL through central DMA; track commands; add interrupt/poll modes; implement bounded reset, re-identification, and re-registration; build a host controller model, QEMU injection, and designated disposable physical test.

### Exit criteria

Host tests cover PRP/SGL, wrap, phase, CID, and status errors; QEMU timeout injection recovers; repeated read/write/flush survives reset; RixFS ordering remains valid; physical logs prove safe operation on the designated device.

## Phase 13 — VFS and RixFS

### Roadmap requirement

Implement vnode/inode/dentry/superblock/file models, mounts/path resolution, file and directory operations, permissions, links/safe traversal, RixFS on-disk structures, journaling/checksums/orphan recovery, and fsck/emergency mount.[1]

### Current implementation

VFS normalizes paths, checks search/file permissions, exposes open/read/write/seek/readdir/stat and mutations, and manages one root mount plus per-process descriptors. RixFS implements persistent inodes/extents/directories, explicit format, validated mount, links, rename, journal replay, checksums, and read-only fsck. Historical QEMU exercised real file utilities on disposable NVMe/RixFS images.[3] [35] [36]

### Status

**PARTIAL / HARDENING REQUIRED.**

### Implemented

Basic VFS/RixFS operations, bad-superblock rejection, persistent metadata, owner/group/other plus ACL checks, hard links, rename, replay, and selected consistency checks exist.

### Missing

Symlink/readlink, stable vnode/dentry lifetime, refcounted open-file descriptions, close-on-exec, mount namespaces, busy unmount, comprehensive orphan/duplicate-extent detection and repair, emergency read-only mount, and generally atomic multi-object transactions are missing.

### Partial

The journal records sector updates, but all crash interleavings of multi-object mutations are not proved atomic. Fsck reports selected inconsistencies but does not repair them. The active VFS has a single root mount and global state.[35] [36]

### Bugs / correctness risks

`lookup_rixfs_path()` returns a pointer to one mutable global `path_node`. `vfs_fd_t` copies offsets across dup/fork instead of sharing an open-file description. `O_CLOEXEC` is declared but not represented. Unmount clears slots without normal endpoint close. Global mounts, FDs, pipes, and path state lack evident SMP locking. A divergent legacy `vfs_file.c` remains.[35]

### Tests

| Class | Evidence | Result |
|---|---|---|
| Host | `rixfs_mount_test`. | Historical bounded PASS. |
| Unit | No symlink, vnode lifetime, shared-offset, fsck repair, or transaction suite. | **MISSING/PARTIAL**. |
| Integration | VFS ↔ RixFS ↔ block ↔ QEMU NVMe. | Historical bounded PASS. |
| QEMU | File/rename/cp/mv/stat/Phase 19 suites. | Functional scenarios only. |
| Physical | No real-filesystem/media acceptance. | **UNVERIFIED**. |

### Validation gaps

Nested lookup lifetime, concurrent rename/open/close, dup/fork shared offsets, append atomicity, CLOEXEC, symlink loops, busy unmount, full disk, corrupt metadata, every journal boundary, orphan repair, and physical power removal remain open.

### Required work

Introduce stable/refcounted vnodes and shared open-file objects; add mount/reference locking and CLOEXEC; implement symlinks or remove declarations; define transaction and flush contracts; extend fsck; add exhaustive fault injection and SMP VFS stress.

### Exit criteria

Path objects remain stable; dup/fork offset semantics and CLOEXEC are correct; exit/unmount closes resources; traversal rejects cycles/bypasses; every interrupted transaction replays atomically or enters deterministic repair/read-only mode; corrupt media never auto-formats; QEMU and physical disposable media remain consistent.

## Phase 14 — Time, RTC and Desktop ACPI

### Roadmap requirement

Provide monotonic and realtime clocks, RTC/CMOS, timers and sleep, desktop ACPI, reboot/poweroff/reset, and idle/thermal safety.[1]

### Current implementation

CMOS RTC conversion and repeated snapshots exist. PIT ticks back monotonic time; realtime is boot RTC plus elapsed PIT time. `nanosleep` busy-yields. ACPI parses FADT S5. Power code has CF9/INT19 reboot and PM1 S5 shutdown paths.[37] [38]

### Status

**PARTIAL / HARDENING REQUIRED.**

### Implemented

RTC conversion, monotonic counter, boot-epoch realtime, sleep syscall, S5 metadata, halt, reboot, and shutdown source exist.

### Missing

Bounded RTC UIP waits, source calibration, drift/adjustment, independent clock-ID behavior, timer queues, APIC/HPET timing, wall-clock policy, physical reset/S5 evidence, idle states, and thermal safety are missing.

### Partial

`rtc_init()` is a no-op. Both POSIX clock IDs route through the same realtime source. Sleep yields instead of blocking. Power paths are source-present but unverified on target hardware.[5] [37] [38]

### Bugs / correctness risks

RTC UIP polling is unbounded and can hang. Reboot/S5 lacks a proved fallback/result protocol. PIT-only time is not SMP calibrated. Realtime cannot be safely adjusted.

### Tests

| Class | Evidence | Result |
|---|---|---|
| Host | ACPI parser and libc time tests. | Partial. |
| Unit | No CMOS model, timer queue, drift, timeout, or power test. | **MISSING**. |
| Integration | `date`, clock and sleep calls in QEMU. | Historical bounded PASS. |
| QEMU | Generic time use. | Reset/S5 **UNVERIFIED**. |
| Physical | No RTC/reboot/power/thermal result. | **BLOCKED**. |

### Validation gaps

Stuck UIP, invalid dates, rollover, monotonic wrap, adjustment, sleep accuracy/interruption, timer load, reboot fallback, S5 outcome, idle wake, and thermal response are untested.

### Required work

Bound RTC access; separate and calibrate clocks; implement timer queues with wait queues; define adjustment; qualify ACPI reset/S5 and safe fallbacks; specify idle/thermal policy.

### Exit criteria

RTC cannot hang; monotonic never regresses; clock IDs are distinct; sleep blocks within measured bounds; reset/poweroff are independently observed in QEMU where possible and on hardware; idle resumes; thermal handling fails safe.

## Phase 15 — USB/xHCI

### Roadmap requirement

Implement xHCI registers, rings/TRBs/cycles, contexts, reset/address/configure, control/bulk/interrupt transfers, DMA/MSI-X, timeout/reset, hotplug, and recovery.[1]

### Current implementation

`xhci.c` contains PCI matching, BIOS handoff, reset, runtime structures, command/event/endpoint rings, slot/address/configure operations, EP0, bulk/interrupt transfers, port reset, polling, attach/detach, and diagnostics. USB descriptor and xHCI capability parsers have host tests.[39] [40]

### Status

**BROKEN for current-tree build integrity; otherwise PARTIAL and hardware BLOCKED.**

### Implemented

Substantial source and historical parser/build evidence exist. A controller-requesting QEMU harness exists.

### Missing

`kernel/usb/xhci_profile.c`, `kernel/usb/xhci_profile.h`, and `tests/xhci_profile_test.c` are referenced but absent at audited HEAD. Ring/command/transfer/reset models, live controller-backed completion, DMA stress, MSI-X integration, endpoint recovery, and physical qualification are also missing.

### Partial

The pending-port queue is bounded. DMA requires physical contiguity for the supported transfer span. Timeout returns an error without Stop/Reset Endpoint, ring rebuild, restart, or quarantine. The generic historical QEMU topology reported zero controllers.[3] [39]

### Bugs / correctness risks

A clean compile cannot resolve the profile source/types/functions. A full 16-entry port queue silently drops transitions. Event-ring serialization is unproved. Timeout can leave poisoned device/ring state. DMA/IOMMU/cache assumptions remain unqualified.[4] [39]

### Tests

| Class | Evidence | Result |
|---|---|---|
| Host | USB descriptors and xHCI capabilities. | Historical PASS; profile target broken. |
| Unit | No TRB/ring/completion/timeout/reset/hotplug/DMA suite. | **MISSING**. |
| Integration | Initialization path exists. | Generic QEMU had controllers=0. |
| QEMU | `qemu_xhci_probe_test.py` requests NEC xHCI + keyboard. | No retained current PASS; **UNVERIFIED**. |
| Physical | Inventory and failing PORTSC history only. | Functional operation **BLOCKED**. |

### Validation gaps

Clean build, profile reconciliation, ring wrap/cycle, stale completion, completion code 11, residuals, hotplug overflow, timeout recovery, composite devices, fragmented DMA, MSI-X, SMP transfers, and physical enumeration are open.

### Required work

Restore/remove profile references consistently; add ring/TRB/context controller models; serialize event ownership; report overflow; implement recovery and central DMA; run controller-backed QEMU and ASUS hardware with raw logs.

### Exit criteria

Clean build/profile tests pass; QEMU enumerates and reads a keyboard through real control/configuration/interrupt-IN; attach/detach and timeout recover repeatedly; UP/SMP4 stress is clean; physical controllers record PCI identity, PORTSC/status, enumeration, and recovery.

## Phase 16 — HID and Input

### Roadmap requirement

Parse HID descriptors/reports, support boot and report protocols for keyboard and mouse, handle rollover/repeat/modifiers, publish an input ABI, clean up hotplug, and prove physical HID input.[1]

### Current implementation

`hid.c` implements bounded report-descriptor parsing, report IDs, keyboard rollover rejection, signed mouse deltas, and boot/report-protocol polling adapters. `hid_report_test` covers parser cases. Kernel main registers/polls keyboards discovered through xHCI; PS/2 keyboard support and bottom-half processing also exist.[39] [41]

### Status

**PARTIAL; xHCI path and physical gate BLOCKED.**

### Implemented

Keyboard/mouse report parsing, boot-protocol helpers, report-ID framing, rollover rejection, signed fields, keyboard registration, and host tests exist.

### Missing

A mouse registry/worker/event path, robust interface-to-endpoint binding, repeat policy, complete supported-usage contract, detach cleanup, live xHCI interrupt-IN evidence, hotplug recovery, unified input ABI qualification, and physical keyboard/mouse evidence are missing.

### Partial

The main integration selects the first interrupt endpoint globally, not necessarily the endpoint belonging to the HID interface. Keyboard registrations are not cleared on detach. Generic QEMU boot proves only that the OS survives with zero xHCI controllers.[3] [39]

### Bugs / correctness risks

Composite devices can bind the wrong endpoint. Polling can continue against a detached/disabled slot. The absent profile files block clean integration. A parser PASS can be mistaken for device PASS even though no live HID reports were observed.

### Tests

| Class | Evidence | Result |
|---|---|---|
| Host | `hid_report_test` positive/negative parser cases. | Historical PASS; current blocked. |
| Unit | Report IDs, truncation, rollover, signed mouse fields. | Useful but incomplete. |
| Integration | Keyboard poll adapter and PS/2 worker. | xHCI data path **UNVERIFIED**. |
| QEMU | Default topology has no xHCI; controller harness lacks retained PASS. | **BLOCKED/UNVERIFIED**. |
| Physical | ASUS note says USB keyboard is not working. | **FAIL/BLOCKED**, not PASS. |

### Validation gaps

Multiple interfaces/report IDs, endpoint ownership, protocol fallback, repeat/modifiers, wheel/buttons, malformed collections, disconnect during transfer, reattach, stale polling, SMP event delivery, and actual key/mouse data are open.

### Required work

Fix Phase 15 first. Bind endpoints to interfaces; add mouse registration and event delivery; clear all registrations on detach; document supported HID subset; expand host tests; qualify emulated and physical keyboard/mouse data and recovery.

### Exit criteria

Correct interfaces/endpoints are selected; keyboard and mouse input events are delivered; malformed reports fail closed; rollover/repeat behavior matches the contract; detach cancels polling and releases state; QEMU and physical cold/warm/hotplug runs provide reproducible input evidence.

## Phase 17 — TTY, PTY and Console

### Roadmap requirement

Implement line discipline, canonical/raw modes, echo, UTF-8 policy, virtual terminals/ANSI rendering, PTYs, job-control hooks, and parser/failure coverage.[1]

### Current implementation

`tty.c` provides canonical/raw input, echo/output queues, UTF-8/VT state, screen buffers, signals, sessions, controlling-terminal hooks, and in-kernel PTY master/slave operations. Host tests cover canonical/raw/echo/PTY/dimensions/ANSI. Historical serial-QEMU logs show control-character prompt recovery.[3] [42]

### Status

**PARTIAL.**

### Implemented

Core TTY state, queues, line-editing primitives, VT/ANSI subset, PTY internals, foreground-group signal hooks, sessions, and deterministic host tests exist.

### Missing

A complete user PTY API, real `ioctl`/termios dispatch, full signal-handler interaction, parser fuzzing, PTY/session QEMU coverage, SMP safety, physical USB-console input, and complete terminal capability policy are missing.

### Partial

PTY functions are predominantly kernel-internal. Userspace `ioctl` lacks command semantics, and `signal()`/`sigaction()` are unsupported. QEMU Ctrl-C/Z/\\ checks assert prompt recovery rather than stopped-job/status behavior.[3] [5]

### Bugs / correctness risks

Global TTY/session/console interactions can deadlock or race with preemption/SMP. Parser state may be exposed to malformed escape streams. Physical console usability depends on the blocked USB/HID path.

### Tests

| Class | Evidence | Result |
|---|---|---|
| Host | `tty_test`. | Historical PASS. |
| Unit | Canonical/raw/echo/PTY/dimensions/ANSI. | Good bounded coverage; no fuzzing. |
| Integration | Serial console → TTY → shell. | Historical named-case PASS. |
| QEMU | Signal/session harnesses and shell. | Partial, marker-based. |
| Physical | No working USB-keyboard/console acceptance. | **BLOCKED**. |

### Validation gaps

Escape fuzzing, queue overflow, malformed Unicode, PTY ABI, termios/ioctl, session ownership, interrupted I/O, multiple VTs, SMP contention, and physical I/O are open.

### Required work

Expose a versioned PTY/termios/ioctl ABI; integrate wait queues/signals; fuzz parser state; add QEMU PTY/session/job-control scenarios; lock global state; qualify serial/framebuffer/USB physical consoles separately.

### Exit criteria

PTY apps work through user ABI; canonical/raw and echo survive malformed input; foreground signals produce correct states/status; queue I/O blocks/wakes safely; QEMU and target consoles remain usable under stress.

## Phase 18 — Shell and Job Control

### Roadmap requirement

Provide tokenization/expansion, redirection, pipelines, conditionals, background jobs, process groups/sessions, foreground signals, job status/reaping, scripting, and robust malformed-input behavior.[1]

### Current implementation

The shell implements bounded lexing, quoting, operators, redirection, variables, arithmetic/command substitution, glob matching, completion, history, pipelines, conditionals, and background launch. Host tests exist. Historical QEMU executes static programs through fork/exec/VFS/pipe/dup2/wait and demonstrates selected signals/pipelines.[3] [43]

### Status

**PARTIAL.**

### Implemented

A meaningful frontend and real execution chain are present. Basic foreground execution, redirections, pipelines, conditionals, background launch, history/completion, and utility composition work in bounded historical QEMU cases.

### Missing

Full job completion/notification, `waitpid(WNOHANG)`, stopped/continued semantics, terminal handoff completeness, signal-specific statuses, reliable full-pipe backpressure, robust scripting/error modes, and broad stress are missing.

### Partial

Prompt recovery after control bytes does not prove signal disposition or wait status. Background launch is evidenced, but lifecycle/notification are open. Reliability inherits waitqueue, exit, descriptor, and pipe defects.[3]

### Bugs / correctness risks

Leaked descriptors can keep pipelines open. Cooperative scheduling can starve jobs. Incomplete signal frames/status macros make reporting misleading. The historical cross-test invalid-opcode fault blocks a broad stress claim.

### Tests

| Class | Evidence | Result |
|---|---|---|
| Host | `shell_test` frontend. | Historical PASS. |
| Unit | Lexer/parser/expansion/glob/completion. | Substantial frontend coverage. |
| Integration | Real shell → process → VFS/IPC chain. | Historical bounded PASS. |
| QEMU | Utility/signal/session/pipe/Phase 19 harnesses. | Partial; job-control closure absent. |
| Physical | No physical interactive-shell acceptance. | **UNVERIFIED/BLOCKED**. |

### Validation gaps

Background completion, stopped/continued jobs, terminal ownership, WNOHANG, writer backpressure, signal status, malformed pointers, nested substitutions, exhaustion, long scripts, SMP, and physical keyboard use are open.

### Required work

Complete waitqueues, signal frames, process lifecycle, shared open files/CLOEXEC, and PTY ABI. Add a job table, notifications, WNOHANG, terminal handoff, exact statuses, and QEMU/SMP large-pipeline stress.

### Exit criteria

Foreground/background/stopped/continued jobs have correct process groups, terminal ownership and statuses; large pipelines finish; descriptors close; malformed input fails without kernel faults; repeated scripts pass UP/SMP4 and a physical interactive run.

## Phase 19 — Base Unix Userland

### Roadmap requirement

Deliver a coherent base command set, including filesystem, text, process, system/storage, hardware and network diagnostics, using real kernel APIs rather than synthetic values.[1]

### Current implementation

`user/programs/` contains a broad bounded subset, and QEMU harnesses exercise file utilities, text pipelines, process tools, cwd/stat, append, and selected negative behavior through the real shell/VFS/RixFS/NVMe path. `docs/PHASE19_KERNEL_API.md` separately specifies still-needed `statfs`, `sysinfo`, `klog_read`, `mount`, and `umount` interfaces.[3] [26] [44]

### Status

**PARTIAL.**

### Implemented

Useful filesystem/text/process/network programs, utility composition, real file mutations, status output for supported APIs, and multiple historical QEMU suites exist.

### Missing

`df`, `free`, `dmesg`, `mount`, and `umount` are absent. Their kernel syscalls and libc wrappers are absent. GPT/partition tools and broad hardware/storage diagnostics are missing. This is not a complete Unix utility environment.[26] [44]

### Partial

The existing programs are a bounded native subset. The API design correctly forbids fake values, but it remains design-only. The design's provisional IDs 137–139 collide with current `DELEGATECAP`, `LIST_PROCESSES`, and `GETRANDOM` assignments.[25] [26]

### Bugs / correctness risks

Implementing the design IDs unchanged could silently invoke security or random syscalls from diagnostics. Utility success can overstate system maturity while capacity/log/mount inspection is unavailable. Several libc APIs used by portable tools intentionally return `ENOSYS`.[5] [26]

### Tests

| Class | Evidence | Result |
|---|---|---|
| Host | Shell/libc tests cover helper behavior. | Historical bounded PASS. |
| Unit | No statfs/sysinfo/klog/mount/GPT utility suite. | **MISSING**. |
| Integration | Utility scripts use shell/VFS/RixFS/NVMe. | Historical bounded PASS. |
| QEMU | Phase 19 extended/utils/file/stat harnesses. | PASS for named scenarios only. |
| Physical | No base-userland run on target storage/input. | **UNVERIFIED/BLOCKED**. |

### Validation gaps

Missing diagnostic APIs/programs, exact errno, malformed pointer/path/descriptor handling, read-only/full-disk behavior, busy mounts, corrupt media, recursive destructive actions, SMP, and physical operation are open.

### Required work

Resolve ABI numbers; implement versioned statfs/sysinfo/klog/mount/umount with privilege and uaccess rules; add real programs; implement GPT before partition tooling; expand negative and composition tests; qualify on physical console/storage.

### Exit criteria

Every roadmap utility exists or is explicitly re-scoped; no program prints synthetic values; diagnostics use noncolliding versioned syscalls; malformed/unauthorized requests return exact errors; QEMU and physical runs verify output against independently observed system state.

## Phase 20 — Multi-user Security and Credentials

### Roadmap requirement

Provide users/groups/credentials, authentication, permission enforcement, sessions and controlling terminals, capabilities, audit identity, safe credential transitions, and complete positive/negative security matrices.[1]

### Current implementation

Process objects track real/effective/saved IDs, groups, sessions, process groups, capabilities, audit UID, and credential transitions. VFS centrally enforces owner/group/other plus ACL access. Set-ID exec, capability drop/delegation, account records, login/session programs, and account-store crash injection exist. Historical QEMU credential/session/account suites provide bounded evidence.[3] [45] [46]

### Status

**PARTIAL / HARDENING REQUIRED.**

### Implemented

UID/GID and supplementary groups, ACLs, setuid/setgid transitions, capabilities, audit UID, sessions/TTY ownership, account storage, permission checks, and selected authorization scenarios exist.

### Missing

Complete authorization and login matrices, user signal-handler delivery, exact per-syscall privilege/error contracts, hardened secret storage/policy, service isolation, physical security/storage evidence, and broad regression closure are missing.

### Partial

Recent logs record credential/auth progress and account crash scenarios, but Phase 20 remains in progress. Historical `killtest` and `capdelegatetest` failures were reproduced on baseline; later fixes still required a complete clean rerun. Userspace `access()` checks mode bits without the kernel's UID/GID/ACL/capability evaluator, producing inconsistent answers.[3] [45]

### Bugs / correctness risks

`openat` ignores dirfd semantics. Many VFS errors collapse to generic `EINVAL`. Exit/session cleanup can leave task or shared-object authority. No IOMMU means device DMA bypasses process/VFS authorization. Intentional signal API stubs limit enforcement and audit behavior at the process boundary.[25] [30]

### Tests

| Class | Evidence | Result |
|---|---|---|
| Host | Credential, ACL, account and parser tests are documented. | Historical, not current. |
| Unit | Selected transition/permission cases. | Incomplete matrix. |
| Integration | Login/session/VFS/account paths. | Historical bounded evidence. |
| QEMU | Phase 20 credential/session/account suites and 12 account crash cases. | Mixed history; full clean rerun pending. |
| Physical | No physical authentication/authorization/storage evidence. | **UNVERIFIED/BLOCKED**. |

### Validation gaps

Root/non-root matrices, all owner/group/other/ACL combinations, saved-ID transitions, kill authorization, capability delegation/revocation, session ownership, audit invariants, malformed pointers, crash cleanup, brute force/rate policy, SMP races, and physical persistence are open.

### Required work

Unify all authorization queries with kernel policy; fix openat/errno/uaccess; complete lifecycle cleanup; re-run previously failing suites; add generated privilege matrices and audit assertions; qualify storage and console behavior on hardware.

### Exit criteria

Previously failing kill/capability cases pass; every privileged operation has positive/negative coverage; `access()` agrees with actual operations; IDs/capabilities cannot be regained improperly; sessions and audit identities survive/terminate correctly; crash and SMP tests are green; physical login/logout evidence is archived.

## Phase 21 — Networking

### Roadmap requirement

Implement Ethernet, ARP, IPv4, ICMP, UDP, TCP ordering/ACK/retransmission/window behavior, blocking/nonblocking sockets, routing, DNS, E1000/QEMU, and physical RTL8125 recovery.[1]

### Current implementation

The stack integrates routing, ARP, Ethernet, IPv4/ICMP/UDP, bounded TCP, socket queues, DHCP/DNS, E1000, and RTL8125 source. Historical QEMU user networking records DHCP, DNS, ARP, ping, TCP handshake, HTTP content/redirects, and packet capture. The Phase 21 exit report explicitly limits closure to software/QEMU and marks physical RTL8125 gates not tested.[3] [47]

### Status

**PARTIAL overall; bounded IPv4/QEMU software scope PASS; physical and full TCP semantics UNVERIFIED/BLOCKED.**

### Implemented

Packet parsing/building, routes, ARP resolution, loopback/external UDP and ICMP, bounded in-order TCP client behavior, sockets, DHCP, DNS, E1000 descriptor paths, RTL8125 register/ring source, ping/curl, and host models exist.

### Missing

Timer-backed TCP retransmission, duplicate-ACK/congestion logic, out-of-order reassembly, dynamic windows, real blocking waits, readiness/poll, server listen/accept/shutdown, sustained traffic, NIC reset recovery, SMP network qualification, and physical RTL8125 operation are missing.

### Partial

TCP drops out-of-order segments and advertises a fixed 4096-byte window. Retransmission is caller-driven/stateless. An empty blocking socket returns EAGAIN rather than sleeping. One unresolved-ARP packet slot bounds progress under bursts. IPv6 has host-tested software foundations but no independent QEMU/physical traffic.[5] [47] [48]

### Bugs / correctness risks

Loss/reordering can stall TCP. Fixed windows and polling can spin or interoperate poorly. A single pending ARP packet can reject/drop concurrent work. Host RTL8125 tests cannot expose PCIe/DMA/PHY/IRQ/reset behavior. No-IOMMU DMA remains a physical security risk.[30] [48]

### Tests

| Class | Evidence | Result |
|---|---|---|
| Host | `net_test`, `e1000_test`, `rtl8125_test`. | Historical PASS; software/model only. |
| Unit | Packet, route, socket, descriptor/register cases. | Useful but no loss/reorder timers. |
| Integration | DHCP/DNS/ARP/TCP/HTTP through E1000 user-net. | Historical bounded PASS. |
| QEMU | ping/curl/external-net harnesses, optional pcap. | PASS for named IPv4 virtual path only. |
| Physical | RTL8125 link/DMA/IRQ/reset/traffic not tested. | **BLOCKED**. |

### Validation gaps

Packet drop/reorder/duplication, RTO/backoff, window exhaustion, receive reassembly, blocking wakeups, server sockets, DHCP renewal, DNS failure, ARP timeout, NIC reset, sustained throughput, SMP4 traffic, independent IPv6 wire evidence, and physical capture are open.

### Required work

Complete timer/waitqueue prerequisites; implement TCP retransmit/reassembly/window/congestion bounds; add blocking/readiness/server semantics; queue ARP-pending traffic; add deterministic netem/fault QEMU and SMP tests; validate RTL8125 using serial plus independent capture.

### Exit criteria

Loss/reorder tests deliver or time out deterministically; window/backpressure is bounded; blocking sockets sleep/wake while nonblocking returns EAGAIN; server tests pass; repeated UP/SMP4 traffic recovers from protocol/NIC faults; physical link, TX/RX DMA, IRQ, reset and sustained traffic are independently observed.

## Phase 22 — Native libc and POSIX Compatibility

### Roadmap requirement

Provide a documented native libc/POSIX compatibility surface: headers/types/errno, memory/string/stdio, allocation, filesystem/process/time wrappers, sockets, signals, pthread-related APIs, tests, and explicit compatibility boundaries.[1]

### Current implementation

`docs/PHASE22_COMPAT.md` defines a static compatibility scope and explicit exclusions. `libc.c` and `unistd.c` implement the bounded surface. `libc_test` covers host behavior, while `posix-test` and `qemu_posix_test.py` cover 27 guest groups, including intentional `ENOSYS` contracts, UDP loopback, socket lifecycle/EAGAIN, exit/atexit, inet/getopt, and process-local mutex behavior.[5] [49]

### Status

**PASS for the explicitly closed historical static scope; HARDENING REQUIRED for robustness; current rerun UNVERIFIED.**

### Implemented

Core headers and types, errno/string/memory/stdio, a brk-backed allocator, filesystem/process/time wrappers, bounded IPv4 sockets, signal masks/pending state, local pthread mutex/once, locale/UTF-8 basics, and documented compatibility limits exist.

### Missing

Dynamic loader/TLS, full pthread/futex threads, signal delivery frames/handlers, blocking sockets, poll/readiness, useful ioctl, file-backed mmap/mprotect/munmap, fd stat/lstat/readlink/symlinks, full locales/timezones, and physical qualification are explicitly deferred or absent.[5]

### Partial

The PASS is a declared static-scope closure, not a general POSIX/Linux claim. `pthread_create/join/detach`, `signal/sigaction`, `listen/accept/shutdown`, `poll`, most `ioctl`, file-backed memory mapping, and several stat/link APIs fail closed with `ENOSYS`.[5] [49]

### Bugs / correctness risks

`free()` marks an allocation unused but the bump allocator does not reclaim/reuse heap space. `realloc()` reads a preceding header before proving the pointer is a valid libc allocation. `WIFEXITED` is always true. Process-local spin mutexes are not a substitute for kernel threads/futexes. Ported software can compile and then fail at runtime if it ignores the compatibility document.[49]

### Tests

| Class | Evidence | Result |
|---|---|---|
| Host | `libc_test`. | Historical PASS; current toolchain blocked. |
| Unit | Strings, allocator basics, stdio, locale, inet, mutex/once and intentional ENOSYS. | Bounded static scope. |
| Integration | Native programs and shell use libc/syscalls. | Historical bounded PASS. |
| QEMU | `qemu_posix_test.py` checks 27 groups and fault markers. | Historical bounded PASS only. |
| Physical | No libc/POSIX target run. | **UNVERIFIED**. |

### Validation gaps

Current-HEAD rerun, allocator invalid pointers/double free/reuse/exhaustion, errno preservation, descriptor overlap, sockaddr length/family, process wait/signal status, long-running heap use, SMP/thread semantics, and physical execution remain open.

### Required work

Restore reproducible tests; harden allocator metadata and reclamation; add negative ABI tests; keep the compatibility document synchronized. Treat dynamic ELF/TLS, mmap, threads/futexes, and signals as named later-phase dependencies rather than silently expanding this closure.

### Exit criteria

The declared static scope passes fresh host and QEMU tests from current HEAD with exact tool/artifact provenance; invalid allocator/API inputs fail safely; all exclusions return documented errors; symbols, headers, tests and compatibility document match; no bounded-scope claim implies unsupported dynamic/thread/signal/physical behavior.

## Cross-Phase Blockers

| Blocker | Affected phases | Evidence | Why it blocks closure |
|---|---|---|---|
| Current tree references missing xHCI profile source/header/test | 00, 15, 16 and any aggregate build | `Makefile`, `kernel/usb/xhci.c`; files absent at HEAD | A clean build/test cannot be claimed even after tool installation. |
| No pinned toolchain, QEMU, CI, or retained artifacts | 00–22 | Makefile and repository inventory; current tools absent | Historical logs cannot establish current reproducibility or artifact identity. |
| Kernel heap is non-reclaiming; PMM reserved ownership is incomplete | 02 and every allocator consumer | `kernel/mm/heap.c`, `kernel/mm/pmm.c` | Leaks and accidental free of live frames undermine long-run, recovery, and preemption work. |
| No general preemption-safe locking model | 02, 05–22 | `kernel/sched/scheduler.c`; preemption rollback in validation log | Timer preemption already exposed allocator corruption; SMP expansion is unsafe. |
| Wait queues do not block scheduler tasks | 05, 06, 09, 14, 17, 18, 21, 22 | `kernel/sync/waitqueue.c`; polling users | Pipes, waits, sleeps and sockets cannot have robust blocking/readiness semantics. |
| Uaccess is validate-then-dereference and exceptions fail-stop | 07–22 | `kernel/mm/uaccess.c`, `kernel/arch/x86_64/idt.c` | Malformed/racing pointers can halt the kernel and prevent complete negative testing. |
| Process/task/object lifetime is not unified | 06, 08, 09, 13, 18, 20, 22 | `kernel/process/process.c`, scheduler, VFS, SHM | Exit/reap/exec/session operations risk stale tasks, mappings, FDs and capabilities. |
| DMA is decentralized and IOMMU is unavailable | 10–16, 21 | `kernel/pci/dma.c`, `kernel/pci/iommu.c` | Virtual success cannot establish physical safety, fragmentation handling, or reset cleanup. |
| Block/NVMe recovery and persistence ordering are incomplete | 11–13, 19, 20 | block cache, NVMe, RixFS source | Filesystem durability and account/security persistence cannot be claimed. |
| USB controller path is unbuilt and unqualified | 15–18, physical usability | missing profile files; zero-controller QEMU; hardware note | Physical keyboard/mouse and interactive acceptance are unavailable. |
| Signal delivery and complete wait statuses are absent | 09, 17, 18, 20, 22 | libc stubs and process signal state | Job control and portable POSIX behavior cannot close. |
| No physical evidence for target boot/device/recovery paths | 01–04, 10–18, 20–22 | hardware docs explicitly separate detection from support | QEMU cannot substitute for firmware, APIC, DMA, NVMe, xHCI, NIC, power, or recovery behavior. |

## Critical Path and Priority Model

| Priority | Meaning | Work on the critical path |
|---|---|---|
| **P0** | Release-blocking correctness or build integrity; do before feature expansion | Restore reproducible build and xHCI profile consistency; fix PMM/heap ownership; implement preemption-safe synchronization and real wait queues; make uaccess fault-safe; unify task/process/FD/SHM lifetimes; establish central DMA; fix block-cache dirty-loss defect. |
| **P1** | Required to close core Phase 00–22 functionality | Reintroduce preemptive SMP scheduling safely; harden VM/ELF; complete pipes/events/signals/job control; add NVMe reset recovery and RixFS transaction/fsck work; close xHCI/HID emulated and physical paths; implement full TCP waits/retransmit/reassembly. |
| **P2** | Required for acceptance breadth and operational completeness | Phase 19 diagnostic/mount APIs, GPT support, APIC timer/timekeeping, VFS symlinks/open descriptions, physical PCI/NVMe/NIC/power qualification, performance and resource-exhaustion tests. |
| **P3** | Important hardening and maintainability after the primary correctness path | ASLR/copy-on-write/demand paging, lockdep depth, richer locales/terminal capabilities, broader IPv6, documentation automation, long-term performance tuning. |

The dependency order is not interchangeable. Preemption depends on memory ownership and synchronization; blocking IPC/sockets/job control depend on scheduler wait queues; storage durability depends on block ordering and NVMe recovery; physical xHCI/NVMe/NIC depends on the central DMA contract; physical system acceptance depends on the reproducible artifact chain.

## Technical Debt Register

| Location | Problem | Impact | Fix | Priority |
|---|---|---|---|---|
| `Makefile:14,254-256`; `kernel/usb/xhci.c` | Missing `xhci_profile` source/header/test are referenced. | Current clean build is deterministically broken once a compiler is present. | Restore tracked files from authoritative design or remove all references and profile use consistently. | P0 |
| `Makefile:28-29` | Wall-clock and dirty-state build ID. | Prevents byte-identical release artifacts. | Add reproducible release mode using `SOURCE_DATE_EPOCH` and external provenance manifest. | P0 |
| `scripts/run-all-tests.sh:2` | `set -uo pipefail` differs from documented `set -euo pipefail`. | Runner/document drift; early command failure may not terminate as claimed. | Reconcile implementation and ledger; add runner self-tests. | P0 |
| `kernel/mm/pmm.c:63-68,115-120` | Kernel/boot/map pages marked used but not permanently reserved. | Erroneous frees can return live frames. | Encode immutable ownership/reservation and test reserved-free rejection. | P0 |
| `kernel/mm/heap.c:16-45` | Monotonic page allocator; `kfree` no-op; oversize failure can leak. | Phase 02 gate fails; long-run exhaustion. | Implement reclaiming allocator, size classes/metadata, rollback, poisoning and accounting. | P0 |
| `kernel/mm/vmm.c:25-37` | Broad writable early identity map. | Runtime W^X is not enforced. | Install final mappings from ELF permissions and remove/limit bootstrap RW aliases. | P0 |
| `kernel/mm/uaccess.c` + `kernel/arch/x86_64/idt.c:102-107` | Validate-then-dereference with fail-stop faults. | User input can halt kernel under race/fault. | Add exception fixups/guarded copy and process-local user-fault policy. | P0 |
| `kernel/sched/scheduler.c:265-337` | Cooperative scheduling; unsafe preemption was reverted. | Phase 06 mandatory gate broken; starvation. | Add preempt-disable discipline, audited locks, per-CPU runqueues and timer/resched IPI. | P0 |
| `kernel/sync/waitqueue.c` | Metadata-only waiters, no scheduler block/wake. | Polling, lost-wakeup risk, incomplete pipes/sockets/sleep. | Atomic condition/enqueue, BLOCKED tasks, timeout/cancel/signal wake. | P0 |
| `kernel/process/process.c:427-438` | Exit/reap lacks unified task, orphan, SHM and waiter teardown. | Stale runnable tasks, leaks, PID reuse hazards. | Refcount task/process objects, retire scheduler tasks, reparent, notify, unmap all objects. | P0 |
| `kernel/storage/block_cache.c:21-29` | Dirty victim invalidated before successful writeback; wrong device sector length can be used. | Data loss and cross-device corruption. | Preserve/reserve victim until success; copy with victim device geometry; test injected failure. | P0 |
| `kernel/pci/iommu.c:3-18` | IOMMU permanently unavailable. | No physical DMA isolation. | Implement DMAR/IVRS domains or explicit restricted no-IOMMU policy with bounce buffers. | P0 |
| `kernel/arch/x86_64/apic.c:67` | x2APIC ID path truncates to 8 bits. | Wrong CPU/IPI/TSS routing on large-ID systems. | Return full x2APIC ID and add >255 fixtures/hardware test. | P1 |
| `kernel/arch/x86_64/smp.c:423-557` | >8 CPUs deferred; partial AP allocations not fully reclaimed. | Target topology unsupported; leaks on AP failures. | Dynamic/supported topology policy and transactional AP resource cleanup. | P1 |
| `boot/efi_main.c:55` | Entry need not lie in executable PT_LOAD; overlaps accepted. | Malformed kernel image ambiguity. | Reject overlaps and require executable-entry containment. | P1 |
| `kernel/vfs/vfs.c:21,75-88` | One mutable global `path_node`. | Lookup results alias/change across calls and races. | Stable refcounted vnode/dentry or copy-out result. | P1 |
| `kernel/vfs/vfs.c:13,111-178` | Dup/fork copy offset/state instead of sharing open-file description; no CLOEXEC. | Incorrect POSIX semantics, pipe leaks/hangs. | Split per-process fd entries from refcounted open-file objects; implement CLOEXEC. | P1 |
| `kernel/ipc/channel.c`; syscall write path | Full pipe returns partial/error without wait/retry. | Pipeline stalls/data-loss ambiguity. | Waitqueue-backed readiness, EAGAIN/EPIPE/EINTR policy and wakeups. | P1 |
| `kernel/storage/nvme.c:65-73` | Two-page PRP limit; timeout leaves controller alive without recovery. | Fragmented/large I/O fails; stale completions/wedges. | PRP lists/SGL, command tracking, reset/reidentify/requeue state machine. | P1 |
| `kernel/fs/rixfs*.c` | Read-only fsck and incompletely proved multi-object atomicity. | Orphans/cross-links and power-loss uncertainty. | Transaction generations, full ownership scan, repair/read-only recovery, injection matrix. | P1 |
| `kernel/usb/xhci.c:1114-1139` | Full pending-port queue drops events silently. | Lost hotplug state. | Coalesce with overflow marker/rescan and serialized ownership. | P1 |
| `kernel/main.c` xHCI/HID integration | First interrupt endpoint chosen globally; detach keeps keyboard registration; no mouse worker. | Wrong composite endpoint and stale polling. | Bind endpoints by interface; unregister/cancel on detach; add mouse registry/events. | P1 |
| `kernel/time/rtc.c` | Unbounded UIP polling. | Physical boot/runtime hang. | Bounded timeout and invalid-RTC fallback. | P1 |
| `kernel/net/socket.c` | Out-of-order TCP dropped, fixed window, caller-driven retransmit, blocking recv returns empty error. | Streams stall under loss/backpressure; poor POSIX behavior. | Timers, reassembly, window accounting, waitqueues, poll/server APIs. | P1 |
| `docs/PHASE19_KERNEL_API.md` vs `kernel/syscall/syscall.h` | Proposed IDs 137–139 collide with active IDs. | Silent ABI misbinding. | Central versioned syscall registry and generated headers/docs. | P1 |
| `user/libc/src/libc.c` allocator | `free` does not reclaim; `realloc` trusts preceding header. | Heap exhaustion and invalid-pointer fault/corruption. | Validated metadata, free list/coalescing, negative tests. | P1 |
| `user/libc/include/sys/wait.h` | `WIFEXITED` is unconditional. | Incorrect job/process status interpretation. | Define actual encoded wait status with signal/stopped/continued cases. | P2 |
| `kernel/tty/tty.c` + libc `ioctl`/signals | Kernel PTY/TTY features lack usable user ABI. | Applications cannot rely on terminal behavior. | Versioned termios/ioctl/PTY and signal-frame ABI. | P2 |

## Validation Matrix

**Meaning:** “Historical PASS” is not current execution. “QEMU-only” is explicitly not physical hardware evidence.

| Phase | Status | Implementation evidence | Host/unit | Integration | QEMU | Physical | Negative/fault | Recovery | Current-head result |
|---:|---|---|---|---|---|---|---|---|---|
| 00 | Partial/Broken | Makefile, linker, checkpoints | Broad historical | Image/ISO historical | Multiple historical harnesses | None | Skip/build drift gaps | Artifact recovery absent | **BLOCKED**: compilers/QEMU absent; missing xHCI files |
| 01 | Partial | EFI loader/handoff | Missing loader unit tests | Loader→kernel | Historical UEFI boot | None | Missing malformed/EBS matrix | One EBS retry only | **UNVERIFIED** |
| 02 | Hardening | PMM/VMM/heap | Missing | Indirect | Ring-3 smoke only | None | Missing allocator/permission faults | Reclaim absent | **UNVERIFIED** |
| 03 | Partial | GDT/TSS/IDT/ISR | GDT pure tests | Boot IRQs | Historical smoke/fault logs | None | Missing vector/iret/nesting matrix | Fail-stop only | **UNVERIFIED** |
| 04 | Partial | ACPI/APIC/IOAPIC/SMP | ACPI/SMP/GDT historical | AP probe/shootdown | Historical WHPX SMP4 | None | Partial parser/protocol negatives | AP cleanup absent | **Pending rerun** |
| 05 | Partial | locks/waiter metadata/workers | No dedicated target | Selected workers | Historical AP worker | None | Missing lock/lost-wakeup | Cancellation absent | **UNVERIFIED** |
| 06 | Broken | process/scheduler/context | No scheduler target | Fork/exec/AP probe | Cooperative historical | None | Preemption attempt failed | No task-lifecycle recovery | **Requirement not met** |
| 07 | Partial | syscall/uaccess | Missing | Broad callers | Selected ABI negatives | None | Fuzz/fault gaps | No uaccess fixup | **UNVERIFIED** |
| 08 | Partial | address-space/ELF/process | Missing | Static executables | Historical ring-3 | None | ELF/VM matrix missing | Exec rollback incomplete | **UNVERIFIED** |
| 09 | Partial | pipe/SHM/groups | Pipe historical | Shell IPC | Bounded pipe/signal | None | Full-writer/death gaps | Cleanup incomplete | **UNVERIFIED** |
| 10 | Partial | PCI/DMA/MSI-X/IOMMU stub | Driver models only | Virtual devices | NVMe/E1000 paths | Inventory only | DMA misuse missing | Reset cleanup missing | **UNVERIFIED** |
| 11 | Partial | block/cache | Missing | RixFS stack | Disposable media | None | Fault backend missing | Cache/reset gaps | **UNVERIFIED** |
| 12 | Partial | NVMe | Missing | RixFS/NVMe | Historical read/write | None | Queue/PRP faults missing | Runtime reset missing | **UNVERIFIED** |
| 13 | Partial | VFS/RixFS/fsck | Mount test | Real utilities | Historical bounded | None | Corruption matrix partial | Replay only; repair absent | **UNVERIFIED** |
| 14 | Partial | RTC/time/power | ACPI/libc partial | Time calls | Generic use | None | Stuck-UIP/power missing | Reset fallback unproved | **UNVERIFIED** |
| 15 | Broken/Blocked | xHCI source; missing profile files | Parser/caps historical | No live default controller | Zero-controller generic run | Failing/inventory only | Ring/timeout gaps | Endpoint reset absent | **BROKEN/BLOCKED** |
| 16 | Partial/Blocked | HID parsers/adapters | HID historical | Keyboard adapter | No live xHCI HID | USB keyboard not working | Parser partial | Detach cleanup absent | **BLOCKED** |
| 17 | Partial | TTY/PTY | TTY historical | Serial shell | Signal/session bounded | None | Fuzz missing | Queue/session recovery partial | **UNVERIFIED** |
| 18 | Partial | shell | Frontend historical | Real commands/pipes | Multiple bounded suites | None | Malformed/stress gaps | Job recovery incomplete | **UNVERIFIED** |
| 19 | Partial | utility subset | Shell/libc partial | Utility suites | Historical bounded | None | API/error gaps | Mount/media recovery absent | **UNVERIFIED** |
| 20 | Partial | credentials/ACL/caps/accounts | Historical partial | Login/session/account | Mixed; rerun pending | None | Matrix incomplete | Account crash only | **UNVERIFIED** |
| 21 | Partial | network/E1000/RTL | Historical models | QEMU user-net | Bounded IPv4 PASS | RTL8125 none | Loss/reorder missing | NIC/TCP recovery missing | **UNVERIFIED** |
| 22 | Bounded PASS | libc/compat report | Historical libc PASS | Native programs | Historical 27 groups | None | Intentional ENOSYS tested | Allocator recovery weak | **Current rerun BLOCKED** |

## Hardware Validation Matrix

| Hardware/domain | Target/evidence available | Functional status | Missing evidence | Required capture |
|---|---|---|---|---|
| UEFI/firmware | OVMF historical; ASUS target specified | QEMU-only | Physical removable boot, map quirks, GOP | Artifact hash, firmware version, full serial, GOP/map summary |
| CPU/SMP/APIC | WHPX SMP4 historical; Ryzen 7 7700 target | Virtual SMP only | 16-thread topology, x2APIC, cold/warm repeat, AP failure | CPUID, CR0/CR3/CR4/EFER, APIC IDs, AP states, ping/shootdown logs |
| Timers/RTC/power | PIT/RTC source | Software/QEMU use only | RTC stuck behavior, calibration, reboot/S5, idle/thermal | RTC values, tick drift, reset/power observation, firmware event/log |
| NVMe | QEMU NVMe real I/O; XPG device specified | QEMU-only | Physical DMA, read/write/flush, timeout/reset, durability | PCI IDs, namespace geometry, status codes, hashes, reset/recovery log |
| GPT/partition | No implementation | **MISSING** | All partition validation | Protective MBR, primary/backup CRC, sector size, GUID evidence |
| xHCI | AMD IDs/profile prose; QEMU harness | Build broken; no functional PASS | Enumeration, commands, transfers, reset/hotplug | PCI ID, profile verdict, PORTSC, completion codes, attach/detach log |
| Keyboard/mouse | HID parser host tests | Parser-only; physical keyboard documented not working | Actual reports/input, detach/reattach, mouse | VID/PID, interface/endpoint, reports, visible input, cleanup log |
| Framebuffer/display | GOP source and generic observations | QEMU/firmware partial | Physical mode/stride/format, sustained rendering | GOP mode, masks, framebuffer range, photographed/serial correlation |
| RTL8125 NIC | Driver source and host model; target `10EC:8125` | **NOT TESTED physically** | Link, TX/RX DMA, IRQ, reset, sustained traffic | PCI/PHY/link status, serial, independent pcap/counters, reset trace |
| E1000 | Historical QEMU user-net | QEMU-only PASS | SMP/fault stress; physical not applicable to target | QEMU version/topology, pcap, repeated fault logs |
| Storage power-loss | Account-store QEMU crash matrix | Bounded account recovery only | RixFS/NVMe/physical mid-write | Injection point, writes/flushes, reboot, replay/fsck, before/after hashes |
| Serial/console | QEMU serial; physical plans | Historical QEMU only | Reliable target COM1 and keyboard console | UART parameters, raw capture, prompt/input/recovery evidence |
| IOMMU | Stub reports unavailable | **MISSING** | DMAR/IVRS translation/isolation | ACPI tables, enabled domains, rejected DMA, fault records |

## Concrete Test Plan

### Functional tests

1. Build every target from a fresh clone using pinned tools, then boot the exact hashed ESP/image/ISO on QEMU UP and SMP4.
2. Exercise loader → kernel → two independent user processes → shell → VFS/RixFS → NVMe and compare free-frame/object counts before and after 1,000 fork/exec/exit cycles.
3. Run producer/consumer pipes larger than 4 KiB, blocking sockets, nanosleep timer wakeups, PTY foreground/background jobs, and signal delivery on UP/SMP4.
4. Perform cached and direct storage read/write/flush with multiple sector sizes and verify hashes after clean reboot.
5. Enumerate xHCI keyboard and mouse, read actual reports, detach/reattach, and verify registry cleanup.
6. Run DHCP, DNS, ICMP, UDP and TCP client/server scenarios; verify output with packet capture.

### Negative tests

1. Fuzz EFI ELF headers/segments, memory-map growth and EBS return codes; assert fail-closed cleanup.
2. Attempt PMM reserved/double frees, bad VAs/PTEs, write-to-text, execute-data and user-to-supervisor access.
3. Inject every exception vector, malformed syscall frame, noncanonical/unmapped/cross-page pointers, stale FDs and wrong object types.
4. Feed malformed ACPI, PCI capabilities, USB descriptors/TRBs, HID reports, ELF files, RixFS structures, network packets and GPT tables.
5. Check unauthorized VFS/security/session/capability operations and exact errno.
6. Exercise zero/full capacities for process, task, FD, pipe, SHM, socket, cache, request and device tables.

### Stress tests

1. Run 10,000 allocator/map/unmap/fork/exec/exit cycles with leak counters and randomized interleavings.
2. Run 24-hour UP and SMP4 mixed workload: pipes, filesystem mutation, NVMe I/O, console, UDP/TCP, timer wakeups and process churn.
3. Saturate pipe/socket/storage queues with multiple readers/writers; measure fairness, latency and bounded memory.
4. Cycle AP work, TLB shootdowns, timer interrupts, NIC traffic and xHCI hotplug concurrently.
5. Repeat cold boot, warm reboot, shutdown and reboot-to-test at least 100 times per supported QEMU and physical topology.

### Fault-injection tests

1. Fail every loader allocation/map/EBS operation and every PMM/heap/page-table allocation site.
2. Interrupt process creation, fork, exec and exit after each ownership transfer; verify rollback and no runnable stale task.
3. Fail block writes/flushes, return short I/O/timeouts, inject stale NVMe CID/phase/status, and reset during commands.
4. Cut RixFS operations after each journal/data/metadata/flush boundary, reboot, replay and fsck.
5. Drop/reorder/duplicate TCP packets; expire ARP/DHCP/DNS; force NIC reset.
6. Inject xHCI command/transfer timeout, completion-code errors, event overflow and disconnect during transfer.

### Recovery tests

1. Verify each failed resource transaction restores object/frame counts and permits a successful retry.
2. Verify AP startup failure degrades safely and releases trampoline/stack/TSS resources.
3. Verify NVMe timeout performs bounded quiesce/reset/reidentify/reregister and I/O resumes without stale completion reuse.
4. Verify filesystem recovery yields either the old or new atomic state, never an unreported hybrid; fsck repair is idempotent.
5. Verify pipe/socket/PTY/device close wakes waiters and returns documented EOF/EPIPE/EINTR/error.
6. Verify NIC/xHCI reset and detach remove stale queues/registrations and permit reattach.

### Physical tests

Use a dedicated disposable disk and the documented Ryzen 7 7700 / ASUS PRIME B650M-R target. Capture raw serial plus independent observations. Run cold/warm boot, all CPU online/IPI/shootdown, RTC, framebuffer, NVMe hashes/flush/reset, xHCI keyboard/mouse reports and hotplug, RTL8125 link/TX/RX/IRQ/reset with pcap, reboot/S5, forced device errors, RixFS power interruption, and final ring-3 shell/POSIX/network runs. A detected PCI ID is not a PASS; each operation and recovery result must be observed.[50] [51]

## Top 20 Remaining Tasks

| Rank | Priority | Task | Dependencies | Exit evidence |
|---:|---|---|---|---|
| 1 | P0 | Restore xHCI profile build consistency. | Authoritative profile design | Clean strict build and profile tests. |
| 2 | P0 | Pin/bootstrap toolchain and add CI/artifact provenance. | None | Reproducible two-build hashes and retained logs. |
| 3 | P0 | Fix PMM permanent reservations and implement reclaiming kernel heap. | Ownership model | Stable allocation counts under pressure. |
| 4 | P0 | Enforce runtime kernel W^X and page-table ownership/reclaim. | Memory model | Permission/fault matrix on UP/SMP4. |
| 5 | P0 | Implement scheduler-integrated wait queues and blocking states. | Task-state contract | Lost-wakeup/timeout/cancel tests. |
| 6 | P0 | Unify task/process/address-space/FD/SHM lifecycle with refcounts. | Wait queues | Fork/exit/reap exhaustion loops. |
| 7 | P0 | Make uaccess fault-safe and freeze syscall ABI IDs/errors. | Exception fixups | Pointer fuzz without kernel halt. |
| 8 | P0 | Establish preemption-safe locking; reintroduce timer preemption. | Tasks 3–7 | Never-yielding hog and allocator stress pass. |
| 9 | P0 | Fix block-cache dirty eviction and define flush/order semantics. | Block contract | Fault backend proves no dirty-data loss. |
| 10 | P0 | Build central DMA/IOMMU-or-bounce contract. | PMM/VMM/PCI ownership | Fragmented/map/unmap/reset tests. |
| 11 | P1 | Complete process threads/TIDs, runqueues, affinity and SMP balancing. | Preemption-safe core | UP/SMP4 fairness and migration evidence. |
| 12 | P1 | Complete pipe/events/SHM cleanup, signal frames and wait statuses. | Wait queues/lifecycle/uaccess | IPC/job-control matrix. |
| 13 | P1 | Harden ELF/exec/VM rollback and user-fault isolation. | Memory/uaccess/lifecycle | Independent concurrent processes and malformed ELF. |
| 14 | P1 | Implement NVMe PRP/SGL and reset/recovery. | DMA + block ordering | Injected timeout/reset then resumed I/O. |
| 15 | P1 | Complete RixFS transactions, orphan detection, fsck repair and power-loss matrix. | Block/NVMe reliability | Deterministic replay/repair across injections. |
| 16 | P1 | Finish xHCI/HID controller, endpoint, hotplug and recovery paths. | Build + DMA | Controller-backed QEMU and physical input. |
| 17 | P1 | Complete TCP and socket blocking/readiness/server semantics. | Timers/wait queues | Drop/reorder/window/server QEMU matrix. |
| 18 | P2 | Implement stable VFS objects/open descriptions/CLOEXEC/symlinks/mount lifecycle. | Lifecycle and locking | POSIX fd/path/concurrency tests. |
| 19 | P2 | Resolve Phase 19 syscall collisions; add diagnostics, mount APIs and GPT. | ABI + storage | User tools and corrupt-media tests. |
| 20 | P2 | Execute complete physical qualification. | All relevant P0/P1 fixes | Archived serial, pcap, hashes, device status and recovery. |

## Recommended Execution Order

1. **Freeze the evidence baseline:** preserve audited HEAD, repair only the missing xHCI profile contract, pin tools, add CI and deterministic artifacts.
2. **Repair foundational ownership:** PMM reservations, reclaiming heap, page-table ownership/reclaim, final W^X, and diagnostics.
3. **Build safe concurrency primitives:** IRQ/preempt contracts, scheduler-integrated wait queues, object refcounts and teardown.
4. **Close the fault boundary:** recoverable uaccess, user-fault containment, syscall registry/error policy, and ABI fuzzing.
5. **Reintroduce preemption in stages:** UP timer preemption, then SMP4 runqueues/migration, with allocator/VM/VFS/IPC audits before each expansion.
6. **Repair blocking user semantics:** pipes, process wait, nanosleep, events, sockets, signals, PTY and job control.
7. **Centralize device I/O ownership:** PCI resources, DMA/IOMMU/bounce, MSI-X, reset teardown.
8. **Close storage correctness:** block queue/order/cache, NVMe PRP/recovery, RixFS transactions/fsck, then GPT/mount APIs.
9. **Close USB/HID:** controller models, controller-backed QEMU, endpoint binding, hotplug/recovery, then physical keyboard/mouse.
10. **Close networking:** TCP timers/reassembly/windows, socket wait/readiness/server APIs, SMP/fault tests, then physical RTL8125.
11. **Finish userland/security compatibility:** stable VFS open descriptions/symlinks, Phase 19 diagnostics, complete Phase 20 matrices, libc hardening.
12. **Run acceptance:** full host/unit/integration/QEMU UP/SMP4, fault/recovery/soak, then physical cold/warm/device/power-loss matrix using the exact retained release artifacts.

## Definition of Done for Phases 00–22

Phases 00–22 are done only when all of the following are true:

1. Every roadmap requirement in Phases 00–22 is implemented or formally removed by an explicit scope decision with downstream dependencies updated.
2. No required path is represented by `ENOSYS`, `SKIP`, `NOT TESTED`, `DEGRADED`, `BLOCKED`, `FAIL`, or `UNSUPPORTED`; Phase 22's bounded static scope remains explicitly bounded until its deferred later phases land.
3. A pinned fresh checkout builds deterministically twice; all artifacts have hashes, provenance, configuration, compiler/linker/QEMU versions, and retained logs.
4. Host/unit tests cover pure parsers and state machines, including positive, boundary, malformed, exhaustion and rollback cases.
5. Integration tests traverse real subsystem boundaries; marker-only boot success is insufficient.
6. QEMU UP and SMP4 pass the complete applicable matrix repeatedly with no exception, panic, timeout, leak trend, stale resource, or hidden skip. QEMU-only evidence is labeled QEMU-only.
7. The documented physical target passes UEFI, SMP/APIC, memory, framebuffer, NVMe, xHCI/HID, RTL8125, RTC, reboot/poweroff and ring-3 scenarios. Detection alone is not support.
8. Fault injection demonstrates rollback/retry or an explicit stable fail-safe state for loader, allocators, processes, block/NVMe, RixFS, USB and networking.
9. Recovery tests include real disposable-media interruption, replay/fsck, device reset, hotplug, waiter wakeup and repeated reboot.
10. Security tests cover uaccess, page permissions, credentials, ACLs, capabilities, sessions, syscall inputs, DMA isolation policy and least privilege.
11. Stress/soak tests cover memory pressure, process/FD/table exhaustion, scheduler fairness, IPC backpressure, storage concurrency, network loss/reorder, USB hotplug and UP/SMP operation.
12. Documentation matches source and tests; every PASS links to immutable evidence for the same commit and topology; all remaining exclusions are clearly marked **UNVERIFIED** rather than implied complete.

## References

[1]: `docs/ROADMAP.md` — Phases 00–22 requirements and definition of complete, especially lines 35–49, 60–290, and 818–847.  
[2]: `docs/CHECKPOINTS.md` — CP0–CP8 evidence classes, failure rules, and phase checkpoints.  
[3]: `docs/VALIDATION_LOG.md` — historical boot, QEMU, SMP, storage, process, networking, suite failures/fixes, and pending revalidation.  
[4]: `Makefile` — strict flags, source lists, tests, image/ISO targets, build ID, xHCI profile references, and conditional skip.  
[5]: `docs/PHASE22_COMPAT.md` — bounded static compatibility scope, guest/host evidence, and explicit deferred APIs.  
[6]: `docs/ARCHITECTURE.md` — intended layering, ABI boundaries, and subsystem dependencies.  
[7]: `linker/kernel.ld` and `docs/IMPLEMENTATION_STATUS.md` — ELF PHDR/GNU_STACK evidence and status boundary.  
[8]: `.gitignore` — generated build artifacts are ignored.  
[9]: `scripts/run-all-tests.sh` — current aggregate-runner shell options and result handling.  
[10]: `boot/efi_main.c` — UEFI loader, ELF validation, GOP/RSDP, map capture and EBS retry.  
[11]: `kernel/boot.S`, `include/kernel.h`, `kernel/main.c` — handoff copy and validation sequence.  
[12]: `kernel/mm/pmm.c`, `kernel/mm/heap.c` — frame ownership and non-reclaiming heap.  
[13]: `kernel/mm/vmm.c`, `kernel/mm/vmm.h` — mappings, early identity map, MMIO and page-table validation.  
[14]: `kernel/arch/x86_64/idt.c` — page-fault diagnostics and fail-stop exception policy.  
[15]: `kernel/mm/uaccess.c` — validate-then-dereference user-copy implementation.  
[16]: `kernel/arch/x86_64/gdt.c`, `idt.c`, `interrupts.S`, `irq.c` — privileged entry machinery.  
[17]: `kernel/arch/x86_64/pit.c` — PIT tick and scheduler accounting hook.  
[18]: `tests/gdt_test.c` and validation history — bounded descriptor evidence.  
[19]: `kernel/arch/x86_64/acpi.c`, `apic.c`, `ioapic.c` — firmware topology and interrupt controllers.  
[20]: `kernel/arch/x86_64/smp.c`, `smp.h` — AP startup, IPI, TLB shootdown and topology limits.  
[21]: `docs/SMP_DESIGN.md` — E-series SMP scope, exclusions and preemption constraints.  
[22]: `kernel/sync/lock.c`, `kernel/sync/waitqueue.c` — synchronization primitives and wait-state model.  
[23]: `kernel/sched/scheduler.c`, `kernel/sched/switch.S` — cooperative scheduler, task slots and context switch.  
[24]: `kernel/process/process.c`, `kernel/ipc/pipe.c`, `kernel/ipc/channel.c`, `kernel/ipc/shared_memory.c` — lifecycle and IPC.  
[25]: `kernel/syscall/syscall.h`, `kernel/syscall/syscall.c` — active syscall table and dispatch.  
[26]: `docs/PHASE19_KERNEL_API.md` — design-only diagnostic/mount API and provisional IDs.  
[27]: `kernel/process/address_space.c` — independent user page-table ownership.  
[28]: `kernel/elf/elf.c` — static ELF validation/loading boundary.  
[29]: `tests/pipe_test.c`, `user/programs/pipe-stress.c`, `scripts/qemu_pipe_stress_test.py` — bounded pipe evidence.  
[30]: `kernel/pci/pci.c`, `dma.c`, `iommu.c`, `msix.c` — PCI/DMA implementation and no-IOMMU boundary.  
[31]: `kernel/arch/x86_64/acpi.c` — MCFG parsing.  
[32]: `kernel/storage/block.c` — block registry and BIO validation.  
[33]: `kernel/storage/block_cache.c` — cache implementation and dirty-eviction risk.  
[34]: `kernel/storage/nvme.c` — queues, Identify, two-page PRP boundary, polling and timeout behavior.  
[35]: `kernel/vfs/vfs.c`, `kernel/vfs/vfs_file.c` — active and legacy VFS implementations, descriptors and path lifetime.  
[36]: `kernel/fs/rixfs.c`, `rixfs_ops.c`, `rixfs_dir.c`, `rixfs_fsck.c` — filesystem, journal and fsck.  
[37]: `kernel/time/rtc.c`, `kernel/time/time.c` — RTC and clock sources.  
[38]: `kernel/power/power.c`, `kernel/arch/x86_64/acpi.c` — reset/power source and ACPI S5 data.  
[39]: `kernel/usb/xhci.c`, `xhci.h`, `usb.c`, `hid.c`, `kernel/main.c` — USB/HID implementation and integration.  
[40]: `tests/usb_descriptor_test.c`, `tests/xhci_caps_test.c` — bounded USB/xHCI host coverage.  
[41]: `tests/hid_report_test.c`, `kernel/arch/x86_64/ps2_keyboard.c` — HID parser and fallback input evidence.  
[42]: `kernel/tty/tty.c`, `tests/tty_test.c` — TTY/PTY implementation and host tests.  
[43]: `user/shell/shell.c`, `tests/shell_test.c` — shell frontend and tests.  
[44]: `user/programs/`, `scripts/qemu_phase19_extended_test.py`, `qemu_phase19_utils_test.py`, `qemu_file_utils_test.py`, `qemu_stat_test.py` — bounded userland evidence.  
[45]: `docs/IMPLEMENTATION_STATUS.md` — explicit validation boundary, Phase 20 in-progress status, physical exclusions.  
[46]: `docs/PHASE20_SECURITY_DESIGN.md` and credential/account source/tests — security design boundary.  
[47]: `docs/PHASE21_EXIT_REPORT.md` — bounded software/QEMU closure and untested physical gates.  
[48]: `kernel/net/stack.c`, `socket.c`, `tcp.c`, `e1000.c`, `rtl8125.c` — networking implementation and TCP/NIC limitations.  
[49]: `user/libc/src/libc.c`, `user/libc/src/unistd.c`, `user/libc/include/`, `tests/libc_test.c`, `scripts/qemu_posix_test.py` — static libc implementation, stubs and tests.  
[50]: `docs/HARDWARE_TARGET.md`, `docs/HW_ASUS_PRIME_B650M_R.md` — target inventory and explicit detection-versus-support boundary.  
[51]: `docs/HARDWARE_FAILURE_AUDIT_2026-09-10.md` — physical risks and prescribed marker/forensics tests.

## Addendum 2026-09-13 (HEAD `2960c35`; audit base `cf77432` unchanged above)

The audit record above is preserved as written. The following deltas
landed after the audited revision and change five of its findings.
Nothing below promotes untested scopes to PASS.

1. **Phase 00 / Phase 15 build-integrity blocker — RESOLVED.**
   `kernel/usb/xhci_profile.{c,h}` and `tests/xhci_profile_test.c`
   were restored (`d0198f0`); `make test` is RC=0 including
   `xhci-profile-test`. A clean tree now builds from checkout.
2. **Phase 13 symlinks — IMPLEMENTED (bounded scope).** Real
   symlinks shipped (`2960c35`): `RIXFS_IFLNK` inodes carry the
   target as file data, VFS follows intermediate and final
   components with a depth cap surfacing as `ELOOP`, new syscalls
   85/88 with libc wrappers and `ln -s`. Host coverage plus QEMU
   traversal/loop/dangle evidence. The "implement symlinks or
   remove declarations" required-work item is closed on the
   implement side; vnode refcounting, CLOEXEC, repair-fsck and the
   rest of the Phase 13 gaps stand.
3. **Phase 02 host tests — PARTIAL, no longer empty.**
   `tests/pmm_test.c` and `tests/heap_test.c` exist and run in
   `make test`. VMM/address-space targets, reclaimability gate,
   poisoning, guard pages and pressure runs remain missing.
4. **Fresh execution evidence at `2960c35`.** `make test` RC=0;
   QEMU `auth`, `cp_mv`, `file_utils`, `phase20_cred`,
   `phase20` (incl. session+auth), `powerloss`, `smp_boot` and
   `ring3` all PASS (see `docs/VALIDATION_LOG.md` 2026-09-13
   entry). The "full suite and UP/SMP4 sanity pending" note is
   closed for this subset; the full 28-suite matrix re-run and all
   physical evidence remain pending.
5. **`281229d` verdict CORRECTED (was: guilty; is: innocent).**
   Serial forensics on a failing run proved the hardening itself
   was never the bug: the first exec'd child faulted with
   `RIP=CR2=0x4034d6d error=0x11`, and that address is
   `x86_enter_user_context` — the user-entry trampoline, which
   `user_entry.S` had placed in `.rodata` (code emitted after a
   `.section .rodata` directive meant for log strings). The NX bit
   correctly refused to execute a data section. Fix-forward:
   trampolines moved back to `.text` with a guard comment, and the
   hardening re-landed with a boot-time `VMM: section perms
   verified R-X/R--/RW-` self-check queried from the live tables.
   The full QEMU matrix re-ran green WITH protections active. The
   Phase 02 W^X exit criterion is now MET in QEMU (PHDR `R E` /
   `R` / `RW`, no `RWE`; runtime flags verified); HW and pressure
   runs remain open. The `1d1a204` revert stays in history as the
   honest record of the misdiagnosis.
