# RixuriOS Master Development Roadmap — v4

**Architecture:** x86_64 / AMD64, 64-bit only  
**Kernel:** freestanding C11/C17 + minimal x86_64 assembly  
**Userspace:** Unix-like, musl/POSIX-oriented, dynamically linked  
**Product principle:** terminal-first, hardware-real, recovery-first  
**GUI:** Phase 35 and absolutely last

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

## Current implementation rule

The roadmap is complete as a plan, but the repository is **not** considered complete. Work proceeds from the earliest unmet gate forward. A subsystem is only promoted after real code, integration and evidence exist. Detached or historical commits do not count unless their files are present on `main`.

### Current engineering focus

`PHASE 02 → PHASE 04 → PHASE 05 → PHASE 06 → PHASE 07 → ... → PHASE 34 → PHASE 35`

Recent foundation work on `main` includes CPU/IDT/GDT/TSS, PMM/VMM/heap, ACPI/LAPIC/IOAPIC/PIT, a kernel-thread context-switch primitive, process lifecycle structures, VFS path normalization, synchronization primitives and a block-device request ABI. These are foundations, not phase-completion evidence.

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

## Definition of done

`correct implementation + real execution + integration + failure handling + regression coverage + security review + measured behavior + documentation`

---

# PHASE 02–35

The detailed phase specifications below remain authoritative. Implementation must advance in dependency order and may not skip an unmet prerequisite merely to make a later feature appear complete.

## Phase gate order

`00 Build → 01 UEFI → 02 CPU/MM → 03 Interrupts → 04 ACPI/SMP → 05 Sync → 06 Scheduler/Process → 07 Syscalls → 08 ELF/User VM → 09 IPC → 10 PCI/DMA → 11 Block → 12 NVMe → 13 VFS/RixFS → 14 Time/Power → 15 USB/xHCI → 16 HID → 17 TTY → 18 Shell → 19 Coreutils → 20 Users/Security → 21 Network → 22 RTL8125 → 23 libc → 24 init/services/pseudo-fs → 25 packages → 26 developer platform → 27 Mayo/observability → 28 audio → 29 graphics/Vulkan → 30 reliability → 31 performance → 32 installer/recovery/update → 33 hardware qualification → 34 pre-GUI certification → 35 GUI`

> The remainder of this document is the existing detailed phase specification. Phase 35 is the GUI/Desktop phase and is deliberately blocked until Phase 34 is released.
