# RixuriOS SMP Design (P0)

## Goal order (no phase starts before the previous gate is green)

- **Phase A — discovery (this change):** MADT CPU enumeration → per-CPU
  table with explicit states. BSP boots exactly as before; APs are only
  *recorded*, never started. QEMU `-smp 4` must boot to shell.
- **Phase B — AP startup:** low-memory (<1MiB) trampoline, INIT-SIPI-SIPI
  via LAPIC ICR, AP entry in 64-bit mode with own stack, park loop.
- **Phase C — per-CPU state:** per-CPU kernel stacks, `cpu_id()` accessor,
  per-CPU scheduler slots, online/offline accounting.
- **Phase D — IPI + TLB shootdown:** IPI ping/pong, `invlpg` broadcast
  with completion counting, VMM shootdown hook.
- **Phase E — scheduler SMP:** per-CPU runqueues, scheduler lock,
  cross-CPU wakeup. Only then may lockless globals be touched.

## Data model (`kernel/arch/x86_64/smp.h`)

```c
smp_cpu_state_t: ABSENT → PRESENT → STARTING → ONLINE / OFFLINE
smp_cpu_t: apic_id u32, enabled, is_bsp, x2apic, state, stack_phys
smp_map_t: cpu[64], count, online, bsp_apic, bsp_index
```

- Table capacity = `ACPI_MAX_CPUS` (64). MADT entries beyond 64 are
  ignored and counted as dropped (logged, never started).
- Disabled (`flags&1 == 0`) CPUs are recorded PRESENT, never started.
- BSP match is by full 32-bit APIC ID (x2APIC-safe).
- If MADT is absent/empty or holds no BSP entry, one synthetic BSP entry
  is appended (fallback=1 on the boot path): the machine always boots UP
  instead of panicking on topology weirdness.
- Phase A publishes `online == 1` (BSP). AP `state` never leaves PRESENT.

## ABI / ownership / locking

- `smp_build_map()` is pure (no HW, no globals): all boot logic is
  host-testable. `smp_discover()` is the only HW-touching wrapper
  (reads MADT snapshot + `lapic_id()`), called once from `main.c`
  pre-scheduler while only the BSP exists — no lock required.
- Published map is read-only after discovery until Phase C introduces
  the smp lock. Writers in later phases: startup (BSP only), hotplug
  (explicitly locked).
- No syscall surface in Phase A. No scheduler changes. No MM changes.

## x86_64 constraints for Phase B (recorded, not implemented)

- Trampoline must live below 1 MiB: `pmm_alloc_page_below(0x100000)`.
- AP starts in 16-bit real mode → far jump to 64-bit entry with BSP-built
  page tables (reuse kernel PML4), own stack, GDT reload, LAPIC enable.
- INIT (assert+deassert) + 2× SIPI with vector = trampoline_page>>12,
  10 ms + 200 µs delays, completion via per-AP `state` flag with timeout.
- QEMU `-smp N` and physical firmware differ in SIPI timing: timeouts are
  mandatory, hangs are a bug.

## QEMU acceptance gates

- A: `-smp 4` boots to `SHELL READY`; log shows `SMP: cpus=4 online=1
  bsp_apic=0`; `-smp 1` and default runs unchanged. DONE (commit 0d6e282).
- B: `SMP: online=4` with per-AP `AP <id> online` lines; no UP
  regression on single-CPU. DONE 2026-09-11 (QEMU evidence, commit 3e59e75):
  root cause of the reset loop was AP `EFER=0x500` (NXE clear) vs NX-marked
  kernel leaves (`#PF` RSVD in `ap_entry` + BIOS IDT = silent triple fault).
  Trampoline now sets NXE next to LME. No PASS claimed for hardware.
- B1 (AP CPU normalization): the trampoline additionally mirrors the BSP
  `vmm_early_init` policy while paging is off — CR4 gets
  MCE|PAE|OSFXSR|OSXMMEXCPT with LA57/PCIDE/SMEP/SMAP/PKE/PGE cleared,
  CR0 gets PE|WP with EM/TS cleared — and long mode loads the snapshotted
  kernel IDTR, so any AP fault reaches the IST#1 handlers with diagnostics
  instead of triple-faulting off the BIOS IDT. Acceptance: `-smp 4`
  still reaches `online=4` + shell, plus QEMU-monitor proof that an AP
  runs with kernel IDT base, BSP-policy CR4 and NXE set. Shared IST#1
  between BSP/APs is accepted for parked Phase-B APs only (documented
  limitation until per-CPU TSS in Phase C).
- B2 (AP descriptor switch): the AP additionally loads the snapshotted
  kernel GDTR, far-returns into kernel CS (0x08), reloads DS/ES/SS,
  nulls FS and loads the kernel TSS. Required because kernel IDT gates
  target kernel CS while the trampoline GDT's 0x08 is a 32-bit segment —
  the first AP interrupt triple-faulted on the CS load with #GP(0x8)
  (`qemu -d int` evidence: `v=0d e=0008` at the park loop during INT 0xE0
  servicing, all gates/IDT/CR3/RSP verified correct). Template stays
  under 0x200; IF stays clear through the whole switch.
- D: IPI ping/pong + shootdown counter test, cross-CPU atomic counter.
- E: 10+ processes across CPUs, no starvation, `preemption_test`.

## Phase D2 (done 2026-09-12): TLB shootdown on unmap

- Single choke point: `address_space_unmap()` (covers brk shrink/
  rollback and shm unmap/destroy paths). After clearing the leaf it
  flushes locally when its own root is current (`vmm_invlpg()`, new tiny
  wrapper — this also fixes a latent UP-only stale-TLB window: the old
  path flushed nothing at all and relied on the next CR3 reload), and
  broadcasts via `smp_shootdown(va)` whenever more than one CPU is
  online (its internal local flush then covers this CPU).
- Gate rationale: no CPU besides the BSP can hold user mappings yet
  (APs never enter userspace, no migration), so `online<=1` skips IPIs
  entirely — zero behavior change on UP and on >8-CPU BSP-only boots.
  Revisit the gate when threads migrate (P5); the hook itself stays.
- Deadlock audit: callers run in syscall/process context, never in IRQ
  context; APs ack without locks; single-flight from one BSP. No new
  host harness (no address_space harness exists); `smp_shootdown`
  itself stays fully unit-tested.
- `vmm_unmap_page_in_pml4` keeps its local-only flush with a comment
  stating the SMP rule; no in-tree caller unmaps shared kernel pages.

## Phase C1 (done 2026-09-11): per-CPU stacks + cpu_id

- `smp_start_aps` allocates a zeroed 16 KiB PMM stack per started AP,
  records the base in `smp_cpu_t.stack_phys` (previously reserved but
  never populated) and passes `base + 16 KiB - 8` as the trampoline
  `stack_top`. The AP no longer runs C code on the 4 KiB trampoline page.
  Alloc failure skips that AP (stays PRESENT, DEGRADED); failed-AP stacks
  stay recorded and are never freed. No guard pages yet (follow-up with
  the per-CPU scheduler).
- `smp_setup_trampoline` stack contract changed accordingly: nonzero +
  8-aligned is required (no longer confined to the trampoline page).
- New `smp_cpu_id()`: LAPIC-ID to map-index scan (`-1` when unknown);
  O(n) is fine for boot/diagnostics, scheduler fast path comes later.
- Scheduler untouched by design: no runqueues, no behavior change on UP.
- Acceptance: `-smp 4` reaches `online=4` + shell with 0 exceptions, and
  GDB shows every parked AP's RSP inside its recorded range
  (cpu1 `0x107ff0` in `[0x104000,0x108000)`, cpu2 `0x10bff0`,
  cpu3 `0x10fff0`, all `state=ONLINE`); `-smp 1` unchanged.

## Non-goals / failure policy

- No CPU hot-unplug until Phase E is stable (OFFLINE is a parked state).
- A failed AP start parks that AP and continues UP+logged; never panic
  the BSP for an AP timeout (documented DEGRADED, not FAIL).
- Physical multi-socket/NUMA is out of scope; uniform LAPIC model only.
