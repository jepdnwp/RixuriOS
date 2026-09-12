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
  (E1 spec 2026-09-12 below: lock + per-CPU current, zero behavior change.)

## Phase E1 (done 2026-09-12): SMP-safe scheduler core, no behavior change

- Motivation: `tasks[32]` + single `current_index` are touched lockless;
  any AP scheduling (E2) or IPI-wakeup would race the BSP today.
- Change (mechanical, `kernel/sched/scheduler.c` only):
  - `current_index` → `cpu_current[SMP_MAX_CPUS]`, routed via
    `sched_cpu()` (`smp_cpu_id`, fallback BSP index, then 0). Zero-init
    is correct: every CPU conceptually starts running `tasks[0]`.
  - One `rix_spinlock_t sched_lock`: irqsave-guarded in create/exit/
    returned paths (arbitrary caller IRQ posture); plain lock/unlock in
    `scheduler_yield` where IRQs are already off by the existing `cli`.
    NEVER held across `rix_context_switch` or `process_activate` — lock
    covers only the `tasks[]` mutation windows (alloc+init, select+
    publish, DEAD-store). IRQ posture across the switch is byte-identical
    to today (`cli` still held, `sti` at the end).
  - Audit: no IRQ/IPI handler takes the lock today (`x86_ipi_dispatch`
    only acks + invlpg; PIT only ticks) — verified, not assumed.
- Explicitly NOT in E1: AP scheduling (APs keep parking), runqueues,
  migration, preemption, host harness (none exists for scheduler —
  create paths need process/user_entry doubles; deferred, not claimed).
- Acceptance: `make test` RC=0, `-smp 1` + `-smp 4` boots identical
  (SHELL READY, `online=4`, ping/shootdown ok, 0 exceptions). ANY
  deviation → revert: `scheduler_yield` is the historically fragile path
  and E1 buys optionality, not features.


## Phase E2 (done 2026-09-12): AP kernel-thread execution + wakeup IPI

- Why not all tasks: the 4 production workers (xhci/serial/kbd/net)
  were written for BSP-only cooperative scheduling; their driver paths
  are NOT audited for true concurrency. E2 therefore adds a per-task
  `ap_ok` affinity (default 0 = BSP-only, exactly today's placement)
  and APs run only `ap_ok` kernel threads. Workers migrate one by one
  in follow-ups with per-driver audits — never silently.
- AP rule (shared `sched_select_locked`, lock held by callers): BSP
  keeps today's any-RUNNABLE selection; non-BSP CPUs take only
  RUNNABLE + `process_pid==0` + `ap_ok` + index!=0 (tasks[0] is BSP
  boot context, never migrates).
- Idle: per-CPU `cpu_idle_rsp/valid`, re-published at the top of every
  `scheduler_ap_idle()` iteration (not once: APs park before
  `scheduler_init` zeroes the table, so the slot self-heals). Loop:
  sti → pick under lock → none: hlt → else publish RUNNING and
  `rix_context_switch` from the idle slot. `yield`'s next==old branch:
  RUNNING current → legacy return; DEAD current → AP with valid idle
  switches back to its idle stack (unlock first), BSP keeps legacy
  spin. No stack variables live across switches (globals only).
- Wakeup: `SMP_IPI_WAKEUP 226` (IST=0 DPL0 gate + `isr226`, EOI-only
  handler — hlt wakes on any interrupt). `smp_wakeup(idx)` single
  (0/-1/-2, no ack wait) + `smp_wakeup_aps()` broadcast, called from
  the create paths AFTER unlock (never holding sched_lock across IPI
  send). Skipped when online<=1: UP boot byte-identical. APs parked
  before scheduler exists wake on the first create after boot.
- Lock audit: no IRQ/IPI handler takes sched_lock (wakeup handler is
  EOI-only); lock covers selection+publish only, never the switch.
  The yield-to-idle switch saves into a per-CPU scratch slot, never
  the dead task's slot (another CPU may already recycle it).
  `cr3trace_push` moves inside the lock (debug-ring coherence under
  real concurrency).
- Proof thread: `main.c` creates one `ap_ok` probe kthread post-workers
  (bounded logs: cpu id, done, exit). Any-CPU execution proves
  scheduling; AP pickup proves E2. Production workers stay BSP-pinned
  until audited.
- Host tests: `smp_wakeup` negatives/send-fail/success+EOI in
  `smp_test` (ping mirror). Pick/idle paths have no scheduler harness
  (documented) — QEMU is the proof.
- Acceptance: UP identical (166 lines); SMP4 `online=4`, pings/
  shootdown ok, shell, 0 exceptions, probe lines present. Revert bar:
  any fault in driver workers (they must NOT move) or missing shell.


## Phase E3 (done 2026-09-12): console serialization

- Why: true concurrency made serial output byte-interleave (E2 tore a
  proof line across two writers). Every future phase's evidence depends
  on clean logs.
- One `console_lock` (`serial.c`, always irqsave — fault/IRQ writers
  exist). Whole-call atomicity for `serial_write/_n/_com1/_com1_n` and
  `serial_read_byte` (serializes the COM1 check-then-read too).
  `tty_output` splits: public wrapper locks (covers syscall-write and
  echo-path FB atomicity per call), `tty_output_nolock` for `serial.c`'s
  internal mirror so one call's UART+FB stay atomic together.
- Guarantee is per-call, not per-line: multi-call log lines can still
  interleave as intact fragments (parseable); torn bytes disappear.
- No forensic variants needed: the nested-fault path halts by design
  (`in_fault` guard) before attempting any logging, so the blocking
  lock cannot self-deadlock — but the old SHARED `in_fault` flag would
  halt a second CPU's forensics whenever two CPUs fault together. E3
  makes it per-CPU (BSP-fallback routing, same as the scheduler).
- Rules (audited, documented): no sleep/yield under the lock (all
  leaves: port IO, memory, FB MMIO); holders never call `panic`;
  `tty.c` never calls back into `serial.c` (no cycle); `serial_drain`
  stays best-effort unlocked (LSR reads only).
- Host: `tty_test` gains spin stubs (additive, smp_test pattern);
  `serial.c` has no host harness (documented).
- Acceptance: UP identical; SMP4 verdicts + byte-clean serial (the
  probe's own `on cpu` line intact — the E2-torn case).


## Phase E4 (done 2026-09-12): input lock + serial-worker migration

- First worker migration (one per phase for bisectability). Chosen:
  `serial_tty_worker` — its whole loop is already-locked calls after
  this phase: `serial_read_byte` (E3 console lock), `tty_input` (new
  input lock), echo-back `serial_write_com1_n` (E3), `yield`.
- New `tty_input_lock` (global, irqsave — `tty_input` already runs in
  ps2-IRQ context today). Wraps whole `tty_input` + `tty_read` bodies.
  Audit: input graph is leaves-only (edit buffers, echo via
  `tty_output`, `signal_hook` which is already IRQ-safe); `tty_read`
  never sleeps inside (callers yield outside). Nesting order is always
  input→output, never reversed (output path is FB/ring only) — stated,
  not enforced.
- Worker logs its CPU once (probe pattern) for placement proof; input
  itself can't be injected through the file-backed serial rig.
- Acceptance: UP identical; SMP4 verdicts + shell + worker-cpu line +
  0 exceptions. Revert bar: any input/echo corruption (compare shell
  prompt behavior) or missing shell.


## Phase E5 (deferred 2026-09-12): keyboard/xhci/net migration

- Audit result: ps2 already has `ps2_lock`, but keyboard↔xhci share
  the HC (event ring/TRBs, pure-poll, no IRQ handler) and the net
  stack is fully unlocked against BSP syscall paths. All three need
  subsystem locks first — and the rig has `xHCI: controllers=0`, so
  HC-lock behavior would be unprovable here. Deferred with reason,
  not forgotten: each migrates with its lock in its own phase.

## Phase E6 (done 2026-09-12): ps2 bottom half (IRQ never takes tty locks)

- Hazard: IRQ1 handler drained the 8042 and ran scancodes straight
  into `tty_input` (input+output locks). A task holding either lock
  with IF=1 plus a keypress deadlocks the machine (handler spins on
  the interrupted holder). trylock on `ps2_lock` never covered it
  (different lock). Invisible in the input-less QEMU rig; fatal on
  hardware with probability per keypress.
- Fix: IRQ appends drained bytes to a 64-entry ring (under `ps2_lock`,
  still trylock-or-return, still never blocks) and returns. The poll
  worker drains HW into the same ring, then processes the ring FIFO in
  task context (IRQs masked by its irqsave) — multi-byte sequences and
  shift state keep their ordering, now single-context. Overflow drops
  newest with a counter (no IRQ-side logging, ever).
- After E6 no IRQ path takes tty/console locks (faults halt by design
  before logging). The E3/E4 locking discipline is then complete.
- No host harness (port IO throughout) — QEMU proof only (identical
  lines; HW keypress proof needs fingers, not claimed here).
- Acceptance: UP/SMP4 identical to E4 counts, shell, 0 exceptions.

## Phase P1 (reverted 2026-09-12): timer preemption is premature

## Phase H1 (done 2026-09-12): hardware xHCI port-reset recovery

- Field report (ASUS PRIME B650M-R, real silicon): boot reaches
  SHELL READY, but every connected port fails attach with rc=4 at
  PORTSC=0x331 (PR stuck, speed 0) — USB keyboard dead, no input.
  QEMU never exercises this (no devices); HW-only path.
- Three changes, one HW cycle (`kernel/usb/xhci.c` only):
  - Timed retry: parked ports re-attempt after 10 s quiet
    (`port_attach_fail_ns`, wrap-safe monotonic compare), not only
    on disconnect. Slow-training links get picked up late; ghost-CCS
    ports just re-fail quietly (existing throttle covers logging).
  - Warm-reset fallback: both USB2-PR failure exits try WPR before
    giving up (the 0x331 escape hatch; harmless no-op on USB2).
  - 4x training window for the ambiguous speed==0 case (~2 s).
  - Recovery prints one line (`port recovered via warm reset`).
- Acceptance: keyboard works on HW (user-verified); QEMU suite
  unchanged (no devices there — compile + boot proof only).


## Phase H2 (spec 2026-09-12): reset escalation (RxDetect + power cycle)

## Phase H3 (spec 2026-09-12): SuperSpeed endpoint companion support

- Scope note (honest): this does NOT fix the USB keyboard (USB2
  device on USB2-speed ports — H2's track). It enables SuperSpeed
  devices (flash drives): companion descriptor parse + burst/ESIT
  programming. No SS device on hand to prove with; proof is host
  unit tests + QEMU no-regression (no devices there either).
- Changes (`kernel/usb/usb.{h,c}`, `xhci.c`, `main.c`,
  `tests/usb_descriptor_test.c` only):
  - New `RIX_USB_DESC_ENDPOINT_COMPANION (0x30)`; endpoint info gains
    `max_burst`, `mult`, `esit_payload`. Parser attaches a companion
    only to the immediately preceding endpoint (validated 6-byte);
    stray/leading companions are ignored, never counted (same
    skip-discipline as today).
  - `xhci_configure_endpoint` programs Max ESIT Payload
    (`MPS*(burst+1)`, SS periodic only; 0 elsewhere — USB2 path
    byte-identical) and the parsed burst.
  - `main.c` passes the parsed burst instead of hardcoded 0.
- Explicitly NOT in H3: SS link management, BOS/LPM, hub depth/TT,
  actual SS hardware proof (needs a USB3 stick on the ASUS box —
  follow-up with the owner).
- Acceptance: `make test` RC=0 incl. new companion asserts, QEMU
  boots unchanged.

- Field evidence round 2 (same ASUS board, H1 ISO): warm reset now
  "recovers" ports, but attach fails one step later at rc=7 (Address
  Device) with PED=0 — the link never reaches Enabled, so the device
  cannot answer. Speeds read valid (1/2/3) on all 4 controllers at
  once: systematic, not signal. H1's WPR "success" was false (WRC
  without PED); H2 verifies PED at every step.
- Escalation inside `xhci_reset_port` (pure PR extracted to
  `xhci_usb2_reset`): PR, RxDetect-force (LWS) + retry, power-cycle +
  retry, warm reset. First ENABLED result wins; recovery prints once
  (`port recovered after reset escalation`). Worst case ~2.5 s per
  failing port on first encounter, then 10 s park. QEMU unaffected
  (no devices, reset never runs).
- Acceptance: keyboard works on HW (user-verified).

## Phase U1 (done 2026-09-12): libc nanosleep NULL-deref fix (rixtest #PF)

- User-reported `rixtest` crash in QEMU: `#PF CR2=0x1 e=5` in ring3
  pid 6 at `abi-negative`'s `nanosleep((void*)1)` probe. Disassembly
  pins it to libc's wrapper (`cmpq $0,(%rdi)` = the `tv_sec<0`
  pre-check): it validated NULL but dereferenced every other pointer
  before the kernel could return EFAULT — exactly what the ABI test
  probes. Sibling wrappers (`openat`, `getdents`, `clock_gettime`)
  pass pointers through untouched.
- Fix (`user/libc/src/unistd.c` only): NULL-check only, pass through.
  Safe because the kernel handler validates everything itself
  (`copy_from_user` → EFAULT; nsec/overflow → EINVAL, including
  negative-valued signed longs seen as huge u64) and the layouts
  match (16-byte sec+nsec words, documented in `time.h`).
- rixtest smoke after fix: 4/6 PASS (`negative_abi`, capdelegatecheck,
  sessiontest, sessionlisttest), 0 faults. `killtest` +
  `capdelegatetest` fail cleanly (status=1, no crash) — A/B-proven
  pre-existing on the 12340ac baseline (signal/capability paths),
  filed, not attempted here.

## Phase S1 (spec 2026-09-12): NVMe flush-timeout wedge (suite failures)

- Symptom: 6/28 suite steps fail (`command not found` after the first
  journaled write; permanent for the boot; silent; nondeterministic;
  disk verified byte-clean except journal scratch).
- Root cause (`kernel/storage/nvme.c:nvme_flush`): on completion
  timeout it sets `c->io_ready=0` with NO diagnostic and returns -6.
  Every later IO then fails fast (-2) forever: one slow virtualized
  flush (QEMU fsync to a Windows file can stall 100 ms+) kills all
  storage for the rest of the boot. The IO path already treats
  timeouts as transient (one-time print, controller stays alive);
  flush did the opposite. A/B-proven pre-existing (fails identically
  on the 12340ac baseline, which never had SMP changes).
- Fix (minimal): flush timeout no longer wedges (`io_ready` kept,
  one-time `NVMe: IO completion timeout` marker like the IO path,
  error still returned so callers fail cleanly with journal semantics
  intact). `pause` every 256 poll iterations in the IO + flush loops
  so a descheduled vCPU/QEMU IO thread progresses instead of burning
  quanta spinning (fewer false timeouts, no other behavior change).
  Limits unchanged. Dead-controller boots now error slowly per-op
  instead of fast-failing — documented, accepted (correctness over
  dead-HW speed).
- Acceptance: full auth sequence green twice in a row + the six
  failing steps re-run green + UP/SMP4 unchanged.

## Phase F1 (done 2026-09-12): extent-exhaustion (adjacency allocator)

- Symptom (surfaced validating S1): `mv` to a fresh name fails with
  `cannot create` although disk/bitmap/inodes are healthy; host dump
  shows the destination dir at all 4 direct extents.
- Root cause: 4 direct extents per inode + first-fit allocator that
  interleaves dir and file-data sectors → a few cumulative single-
  sector growths exhaust a dir's extents on a nearly-empty disk, and
  every later growth (`dir_append`, rename-new-sector, file `grow`)
  fails permanently. Instrumented to `append_extent` via staged
  returns (no guest forensics needed beyond serial markers).
- Fix (format-compatible, no layout change): `append_extent` also
  coalesces backwards; new `alloc_sec_near` tries the file's own
  extent ends/starts first and falls back to the old scan;
  `grow`/`dir_append`/rename-new-sector pass their extents.
  Fragmentation collapses in the common case; the 4-extent ceiling
  remains (documented limitation, not a silent wedge).
- Acceptance: the bisect sequence (init/renametest/cp/mv) green +
  full cp_mv suite green + powerloss matrix re-run (allocator layout
  changed) + UP/SMP4 unchanged.

## Phase U2 (done 2026-09-12): errno-convention family (test programs)

- libc wrappers return -1 with errno (`rix_int_result`); a family of
  test programs compared raw negatives instead: authcheck
  `protected` (`fd != -EACCES`), killtest (`read == -EINTR`,
  `kill != -EACCES`), capdelegatetest `expect_error`
  (`result == -error`), metatest chown/stat (`==/-EACCES/EINVAL`),
  renametest stat/rename (`!=-EINVAL/EEXIST`). All flipped to
  `result<0 && errno==` form (+ `<errno.h>` includes). Internal
  -1/-2 conventions (ping `echo_rounds`, uniq `read_line`) verified
  correct and untouched.
- Acceptance: auth 13/13 markers, killtest/renametest/capdelegatetest
  markers, full suite re-run.

- Attempted (slice: BSP quantum; full: RESCHED-IPI 227 + hog proof):
  green TWICE on UP/SMP4 — then a silent UP hang on the third boot.
- Root cause (two layers, both real): (1) never-yielding tasks inherit
  IF=0 from the switch, freezing PIT — fixed via sti-before-switch;
  (2) the actual killer: IF=1-everywhere exposes the kernel's
  unlocked allocators (pmm/heap/vmm/process) to IRQ-yield preemption.
  A quantum landing mid-allocator corrupts state via a second task's
  allocation. Pre-P1 IF≈0 accidentally shielded all of it.
- Verdict: timer preemption needs a kernel-wide preempt-safety
  retrofit (preempt-disable windows or fine-grained allocator locks)
  — its own project, not a slice. Fully backed out (vector 227,
  broadcast, hog, IRQ-yield split all removed); the tree is
  cooperative again (E4 shape). The slice/full green runs are kept in
  the log as evidence of mechanism, not of safety. E6 (ps2 bottom
  half) stands as filed.


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

## Phase C2 (done 2026-09-12): per-CPU TSS

- Problem: one shared `tss` + one shared 4 KiB double-fault stack; every
  AP `ltr`s selector 0x28 onto it (D1b). Parked APs never touch RSP0, but
  any AP fault already switches to the SHARED IST[0] stack, and
  `tss_set_rsp0` (per-task, `process_activate`) writes the shared TSS —
  one concurrent BSP/AP fault or the first AP task corrupts state.
- Design: per-AP GDT COPY (same 7-entry layout, selector map unchanged —
  no trampoline asm change, `ltr $0x28` keeps working). Per started AP
  the BSP allocates 2 PMM pages: page A holds the GDT copy at +0 and the
  `x86_tss_t` at +64; page B is the zeroed 4 KiB DF stack. TSS init
  mirrors the BSP (`rsp0` = AP stack top, `ist[0]` = DF top,
  `iomap_base` = sizeof, rest zero). The trampoline DATA GDTR slot gets
  the per-AP GDTR (limit 55, base = physmap VA of the copy) INSTEAD of
  the kernel snapshot; IDTR snapshot is unchanged. Descriptors use
  physmap VAs (valid when the AP runs long-mode on the shared tables).
- Routing: `tss_set_rsp0`/`tss_current` resolve the current CPU via
  `tss_cpu_index()` — weak default -1 in `gdt.c`, strong override in
  `smp.c` returning `smp_cpu_id()` (established weak-stub pattern, no new
  include edges). Index valid + registered → per-CPU TSS, else the BSP
  static TSS (early boot, unknown CPU, >8-CPU deferred path all keep
  today's behavior). Registration: `tss_register_cpu(i, tss_va)` at
  bringup; BSP never registers (static TSS untouched — "BSP boots
  exactly as before").
- Failure policy: any per-AP alloc/build failure skips that AP
  (DEGRADED, same as C1 stacks). `>8`-CPU topologies allocate nothing.
- Host tests: new pure builder `gdt_build_cpu_copy()` (constants shared
  with `gdt_init`, TSS desc via the same helper) + `tests/gdt_test.c`;
  `smp_test.c` harness enlarged (any-count PMM arena, wider physmap
  fake) with asserts for recorded `tss_page_phys`/`df_stack_phys`,
  distinctness, and GDTR-slot content. No production-logic weakening.
- Acceptance: `make test` RC=0, `-smp 1` SHELL READY clean, `-smp 4`
  reaches `online=4` + shell with 0 exceptions (a bad per-AP GDTR/TSS
  desc triple-faults at the first IPI/park exactly like the D1b class,
  so clean boot is the proof), plus distinct per-AP `tss=`/`df=` phys
  lines in the boot log. QEMU cannot distinguish the switch by selector
  (still 0x28) — noted, not claimed. DONE 2026-09-12 (WHPX `-smp 4`:
  distinct TSS/DF pages per AP, `online=4`, shell, 0 exceptions).

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
