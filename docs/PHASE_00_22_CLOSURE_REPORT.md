# RixuriOS Phase 00–22 Closure Report

**HEAD:** `320930e` + P5-slice-1 working tree (see `git status`; commits pending)
**Date (UTC):** 2026-09-15
**Rule:** 12-criterion closure rule. `PASS` only when implementation, happy-path,
negative, integration, QEMU, SMP where applicable, physical where required,
recovery, security, no known blocker, docs match, and current HEAD all hold.
Otherwise `PARTIAL` / `HARDENING REQUIRED` / `BROKEN` / `BLOCKED` / `UNVERIFIED`.
**Verdict:** Phase 23 remains **LOCKED**. No Phase 00–22 phase is marked `PASS`.

This report re-audits current HEAD (not the 2026-09-12 `cf77432` gap-analysis
revision). That gap analysis is superseded where HEAD has since landed fixes
(xHCI profile restored, PMM reserved bits, heap reuse, VMM map lock, sync
primitives, R1–R4 scheduler, F1/F2/F4/F5 uaccess/fault/fuzz/ABI, NVMe retry,
plus this slice's block-cache/ELF/RTC/libc/VMM/repro work). Remaining gaps are
stated plainly; QEMU PASS is never claimed as physical PASS.

## Cross-cutting evidence in this slice

- `make test CROSS=x86_64-linux-gnu- HOST_CC=gcc` RC=0 with `-Wall -Wextra -Werror`,
  including new `block_cache_test` and `elf_test` plus extended `libc_test`.
- `make image CROSS=x86_64-linux-gnu-` RC=0.
- QEMU (8.2.2): `qemu_pipe_stress_test`, `qemu_crash_test`, `qemu_fuzz_test`
  PASS, zero fault markers. Full 37-harness `run-all-tests.sh` rerun open.
- `SOURCE_DATE_EPOCH` two-build `build/kernel.elf` sha256 identical.
- `git diff --check` clean.
- Physical HW: no new evidence in this slice; all HW gates stay BLOCKED.

## Phase 00 — Governance and Reproducible Build — PARTIAL / HARDENING REQUIRED

- Implemented: canonical Makefile, `-Werror`, ELF checks, image/ISO targets,
  host/QEMU harnesses, checkpoint ledger, `TOOLCHAIN.md` pins,
  `SOURCE_DATE_EPOCH` deterministic `build_id.h` (kernel ELF repro proven),
  CI workflow (host+image+fast QEMU+repro probe).
- Remaining: FAT/ISO timestamp normalisation, SBOM/provenance manifest,
  artifact retention, test-skip enforcement, ABI/version ledger automation,
  release-blocker policy.
- Tests: host suite green incl. new tests; `git diff --check` clean.
- QEMU: fast subset green; full matrix rerun open.
- Physical: none (build-to-HW artifact chain unproven).
- Limitations: image/ISO byte-identity not claimed.
- Security: build sandboxing, supply-chain verification open.
- Recovery: artifact recovery absent.
- Final: **PARTIAL**.

## Phase 01 — UEFI Boot and Firmware Handoff — PARTIAL

- Implemented: ELF64 validation/PT_LOAD copy/zero, ACPI/GOP discovery, map
  capture with growth retries, single EBS retry, versioned handoff, QEMU boot
  to `KERNEL_READY` + shell.
- Remaining: EFI mock harness, malformed-loader corpus, EBS fault injection,
  GOP format/stride validation, allocation rollback for all loader failures,
  executable-entry + non-overlap enforcement in loader (kernel ELF now
  enforces; loader still aggregate-range only), physical boot log.
- Tests: no loader unit tests.
- QEMU: historical + current boot smoke green.
- Physical: BLOCKED/UNVERIFIED.
- Limitations: firmware quirks, removable-media matrix open.
- Security: handoff version/size checks exist; framebuffer bounds incomplete.
- Recovery: single EBS retry only.
- Final: **PARTIAL**.

## Phase 02 — CPU, PMM, VMM and Kernel Memory — HARDENING REQUIRED (reclaim gate open)

- Implemented: PMM reserved-bit ownership + IRQ-safe locks + lockdep, heap
  reuse with split + alloc rollback + record-exhaustion fail-closed, VMM map
  lock + section R-X/R--/RW- verify + map-failure prune + unmap reclaim +
  huge-split-before-unmap, user/supervisor flags, shootdown hooks, host
  `pmm_test`/`heap_test` + new `block_cache_test`/`elf_test` indirectly.
- Remaining: leak accounting, corruption canaries/poison/quarantine, guard
  pages, DMA-capable allocation contract + central map/pin/SG API, full
  page-ownership audit, huge-split rollback proof, sustained pressure/soak +
  SMP4 memory stress, supervisor/user isolation matrix on HW.
- Tests: host allocator/VMM unit green; exhaustion/alignment/rollback covered
  for heap; PMM reserved-free/double-free covered; W^X verified on boot.
- QEMU: ring-3 + fork/exec stress green; targeted permission-fault matrix open.
- Physical: UNVERIFIED (map diversity, pressure).
- Limitations: 256 heap records ceiling with clean failure; no OOM policy.
- Security: W^X on; SMEP/SMAP, stack guards open.
- Recovery: alloc failures roll back; page-table prune on failure.
- Final: **HARDENING REQUIRED**.

## Phase 03 — GDT/TSS/IDT/Exceptions/Interrupts — PARTIAL / HARDENING REQUIRED

- Implemented: GDT/TSS/IST1, IDT 0–31 + IRQs + IPIs + DPL3 0x80, error-code
  stubs, EOI, fault forensics with walk dumps, F2 user-fault kill matrix
  (CPL3 0,1,3,4,5,6,13,14,16,17,19 → exit 139), fixup hook for uaccess.
- Remaining: full vector-injection matrix, malformed-iretq rejection proof,
  IRQ nesting policy, spurious IRQ regression, double-fault exhaustion,
  per-CPU GDT/TSS on HW, guard pages.
- Tests: `gdt_test` + boot selftest + crash/fuzz green.
- QEMU: fault-kill + fuzz green; vector matrix open.
- Physical: BLOCKED.
- Limitations: fail-stop for kernel faults by design.
- Security: user faults contained to process; kernel faults freeze with
  forensics.
- Recovery: fixup for uaccess only; no general exception recovery.
- Final: **PARTIAL**.

## Phase 04 — ACPI/LAPIC/IOAPIC/Timers/SMP — PARTIAL

- Implemented: RSDP/XSDT/RSDT validation, MADT/x2APIC/IOAPIC/ISO/MCFG/FADT-S5
  parsing, LAPIC/x2APIC, INIT/SIPI/IPI, IOAPIC routing, PIT ticks, AP startup
  with stacks/GDT/TSS, per-CPU state, shootdown, `smp_boot` + E7/R2/R3 SMP
  evidence.
- Remaining: APIC timer/HPET, x2APIC ID >255 full path, >8 CPU policy,
  AP offline/restart + resource reclaim, stack guards, sustained SMP soak,
  HW topology (Ryzen 16-thread) evidence.
- Tests: `acpi_test`/`smp_test`/`gdt_test` green; QEMU SMP2 preempt green.
- QEMU: UP + SMP green for covered paths.
- Physical: BLOCKED.
- Limitations: PIT-only timing; 10ms tick floor.
- Security: firmware pointer range validation partial.
- Recovery: partial AP-failure degradation only.
- Final: **PARTIAL**.

## Phase 05 — Synchronization and Kernel Workers — PARTIAL / HARDENING REQUIRED

- Implemented: spin/IRQ-save, mutex, RW, sem, refcount, lockdep-lite with
  allocator/VMM/process/NVMe wiring, bounded waiter metadata + P3
  scheduler-backed blocking (pipe readers block, signal wake, VFS/PROC/NVMe
  ordered locks).
- Remaining: generic wait-queue timeout/cancellation API, priority
  inheritance, deadlock diagnostics coverage, worker join/drain lifecycle,
  SMP contention stress, IRQ-context misuse matrix.
- Tests: `sync_test` green; pipe/smp paths green.
- QEMU: AP probe + pipe block/wake green.
- Physical: UNVERIFIED.
- Limitations: single `sched_lock` retained by design at this scale.
- Security: lock ordering enforced via lockdep ranks.
- Recovery: cancellation paths partial.
- Final: **PARTIAL**.

## Phase 06 — Processes, Threads and Preemptive Scheduler — PARTIAL / HARDENING (QEMU scope complete)

- Implemented: PID/TID (R1 monotonic, no reuse), process/thread objects,
  kernel threads, user contexts/stacks, BSP + symmetric AP preemption (tick
  quantum + IRQ-return yield + FPU images), per-CPU runqueues (R2 + verify),
  affinity/migration (R3 + counters), priority/accounting (R4 4:1 share),
  slot-burst proof, hog/interleave/fair/migrate harnesses.
- Remaining: shared-address-space clone (Phase 25), >2 priorities/aging,
  wake-latency histograms, per-CPU locks (not needed at 32 slots), HW
  preemptive SMP + >8 CPU + HW timer behavior.
- Tests: `thread_test`/`runqueue_test` + QEMU hog/fair/migrate/burst green.
- QEMU: UP + SMP2 green; SMP4 spot-check green (R4 log).
- Physical: BLOCKED.
- Limitations: 32 tasks / 64 threads / 128 procs ceilings with clean `-1`.
- Security: affinity cannot strand tasks; boost kernel-only.
- Recovery: DETACHED/ZOMBIE self-clearing paths proven.
- Final: **PARTIAL** (phase-level; QEMU scope complete per re-audit).

## Phase 07 — Syscall ABI and Uaccess — PARTIAL / HARDENING REQUIRED

- Implemented: `int 0x80` dispatch, v1 additive registry
  (`docs/ABI_REGISTRY.md`, 77 numbers, ENOSYS for reserved), canonical/range
  checks, `copy_from/to_user` with validate-fast-path + fixup-armed raw
  copies (preempt-disabled, per-CPU single-consume), `-EFAULT` unification,
  `abi-negative` + `crashtest` + 6000-call `fuzztest` green.
- Remaining: concurrent-unmap fuzz matrix beyond boot selftest, per-call
  errno/restart/cancel spec, ioctl/poll/mmap reserved surface, HW fault
  evidence.
- Tests: host + QEMU negative/fuzz green.
- QEMU: crash/fuzz green.
- Physical: UNVERIFIED.
- Limitations: fixup covers only the two copy loops.
- Security: bad pointers return EFAULT or kill process; kernel-side faults
  still freeze (correct).
- Recovery: fixup resume proven on boot.
- Final: **PARTIAL**.

## Phase 08 — User VM and ELF64 Execution — PARTIAL

- Implemented: independent PML4 roots, owned/shared user pages, clone,
  teardown, static load with BSS/W^X, 32-page stacks, argc/argv/envp/auxv +
  AT_NULL + 16B alignment, fork/exec atomic replacement, this slice's
  overlap + executable-entry enforcement with host corpus.
- Remaining: ET_EXEC/ET_DYN/PIE/INTERP policy doc, ASLR, stack guards, COW,
  demand paging, exec rollback stress, CR3/TLB race matrix, HW ring-3 proof.
- Tests: new `elf_test` green; no address-space host model.
- QEMU: static fork/exec/xargs/pipe-stress green.
- Physical: BLOCKED.
- Limitations: static-only by design until Phase 23.
- Security: W+X rejected; entry must be X.
- Recovery: exec replacement atomic on failure.
- Final: **PARTIAL**.

## Phase 09 — IPC and Process Communication — PARTIAL / HARDENING REQUIRED

- Implemented: 4096B channel pipes, dup/fork-retained refs, stdio reservation,
  blocking readers with write/close wake, EOF/EPIPE/partial semantics,
  SHM create/map/unmap/destroy, process groups/signals pending/mask,
  shell pipelines + pipe-stress (8 rounds) green.
- Remaining: blocking writer/backpressure contract, FIFOs, events/wait
  objects, Unix sockets + FD passing, full signal frames, death/exec SHM
  teardown proof, SMP contention.
- Tests: `pipe_test` + QEMU pipe-stress green.
- QEMU: bounded pipe/signal green; full-writer stall still open by design.
- Physical: UNVERIFIED.
- Limitations: >4096B two-write stalls (documented, not claimed).
- Security: endpoint refcounting; authorization for SHM partial.
- Recovery: close wakes readers; writer path incomplete.
- Final: **PARTIAL**.

## Phase 10 — PCIe, MCFG, MMIO and DMA — PARTIAL / HARDENING REQUIRED

- Implemented: PCI/ECAM discovery, capability/BAR sizing/binding, uncached
  MMIO window with dedup + kernel-slot borrow, page-list DMA alloc/free
  below 4G, central map/unmap/SG/is_mapped/sync/bounce ownership contract
  (per-page PMM validation, owner+direction, overlap reject, SG rollback,
  mfence sync, after-free containment, lockdep rank 25), MSI-X helpers,
  IOMMU fail-closed stub. Host `dma_test` green.
- Remaining: BAR ownership/release, bridge/multifunction qualification, hot
  removal, cache-coherency rules per device, MSI-X lifecycle, DMAR/IVRS
  domains or explicit restricted no-IOMMU policy, driver migration to the
  central API (NVMe queues/list now mapped; E1000/RTL8125/xHCI still use
  direct PMM helpers).
- Tests: E1000/RTL8125/xHCI-caps + new `dma_test` green.
- QEMU: NVMe/E1000 paths green.
- Physical: inventory only; operation BLOCKED.
- Limitations: identity translation (no IOMMU); DMA is physical pages with
  ownership records, not device-domain remapping.
- Security: unmapped/arbitrary DMA rejected at the API; no-IOMMU escape still
  possible for drivers bypassing the API (documented risk).
- Recovery: reset cleanup open.
- Final: **PARTIAL**.

## Phase 11 — Storage Core — PARTIAL / HARDENING REQUIRED

- Implemented: block registry + validated `block_submit`, 64-entry
  writeback cache with this slice's durability fix (preserve-on-failure,
  victim-size correctness, second-victim safety, flush redirty),
  per-device flush, RixFS integration.
- Remaining: request queues, async completion, SG, ordering domains,
  FUA/barriers, generic retry/reset, queue-full policy, terminal errors,
  metrics, fake-backend concurrency/order matrix, power-loss matrix.
- Tests: new `block_cache_test` fault-injection green.
- QEMU: NVMe→cache→RixFS file utilities green.
- Physical: UNVERIFIED.
- Limitations: synchronous dispatch only.
- Security: bounds/overflow checks in submit.
- Recovery: dirty preserved on failure (this slice); reset recovery open.
- Final: **PARTIAL**.

## Phase 12 — NVMe — PARTIAL (physical + recovery BLOCKED)

- Implemented: reset/enable, admin + I/O queues, Identify, namespace
  registration, PRP1/PRP2 + single-page PRP-list builder (pure
  `nvme_prp_build`, 257-page / 1M cap) with per-controller static list page,
  per-page PMM validation, list-before-doorbell ordering, DMA-mapped queues
  + list, polling read/write/flush, single bounded retry on transient
  timeout, QEMU mount + file I/O green. Host `nvme_prp_test` green.
- Remaining: SGL, multiple outstanding commands + CID tracking, interrupt
  completions, stale-CID handling, runtime reset/re-identify/re-register/
  quiesce FSM, 4K LBA + large-transfer + concurrency matrix, HW
  qualification.
- Tests: new PRP host model green; queue/reset host model still open.
- QEMU: Identify/mount/read/write green (1-sector cache path); large-transfer
  list path built but without multi-block HW proof; reset matrix open.
- Physical: BLOCKED.
- Limitations: single outstanding per controller (io_lock held across poll)
  by design; static list page shared under that lock.
- Security: data pages validated live + DMA-mapped; cache assumptions still
  unqualified on HW.
- Recovery: timeout returns error without wedge; full recovery open.
- Final: **PARTIAL**.

## Phase 13 — VFS and RixFS — PARTIAL / HARDENING REQUIRED

- Implemented: VFS normalize + permission checks + open/read/write/seek/
  readdir/stat/mkdir/unlink/rmdir/rename/link/symlink/readlink, per-process
  FDs with `O_CLOEXEC`/`FD_CLOEXEC` (open sets, dup family clears, fork
  preserves, `F_GETFD`/`F_SETFD` served, `execve` closes cloexec only on
  success), RixFS inodes/extents/dirs, format/mount validation, hard links,
  journal replay, checksums, read-only fsck, compact-dirent + replace-rename
  fixes, host `rixfs_mount_test` + `symlink_test` green, QEMU `cloexec-test`
  green.
- Remaining: stable refcounted vnode/dentry, shared open-file descriptions
  (dup/fork offsets still copied), busy unmount, mount namespaces,
  orphan/duplicate-extent repair, emergency read-only, atomic multi-object
  transactions, SMP locking, legacy-divergence audit complete.
- Tests: host mount/symlink green; QEMU file/cp/mv/rename + new cloexec
  suites green.
- QEMU: disposable-image functional green; durability/crash matrix partial.
- Physical: UNVERIFIED.
- Limitations: single root mount; fsck reports, limited repair.
- Security: traversal + permission central checks; TOCTOU/symlink races open.
- Recovery: replay only; repair/read-only open.
- Final: **PARTIAL**.

## Phase 14 — Time, RTC and Desktop ACPI — PARTIAL / HARDENING REQUIRED

- Implemented: CMOS RTC conversion + snapshots, PIT monotonic, boot-RTC +
  elapsed realtime, tick-driven `nanosleep` (`time_sleep_queue` + PIT wake,
  prepare-recheck-block) + `/bin/sleep`, FADT S5 parse, CF9/INT19 reboot +
  PM1 S5 paths, bounded UIP.
- Remaining: RTC init + invalid-fallback, calibration/drift/adjustment,
  distinct clock IDs, APIC/HPET timing,
  wall-clock policy, HW reset/S5 + idle/thermal proof.
- Tests: ACPI/libc time partial; no CMOS/timer-queue model.
- QEMU: date/sleep green; reset/S5 unverified.
- Physical: BLOCKED.
- Limitations: sleep yields; PIT-only; 10ms granularity.
- Security: bounded RTC cannot hang boot (this slice).
- Recovery: reboot/S5 fallback unproven.
- Final: **PARTIAL**.

## Phase 15 — USB/xHCI — PARTIAL / HW-BLOCKED (build integrity restored)

- Implemented: PCI match, BIOS handoff, reset, rings/TRBs/cycles, slots,
  address/configure, EP0/bulk/interrupt, per-DCI rings, port reset, polling,
  attach/detach, enumeration + HID hookup, profile DB
  (`xhci_profile.c/h` + host test green), H1–H4 + hotplug probe.
- Remaining: ring/TRB/completion host models, live controller-backed
  completion, DMA stress, MSI-X integration, timeout Stop/Reset Endpoint +
  ring rebuild, hotplug overflow policy, composite-device matrix, HW
  enumeration + keyboard proof (H5).
- Tests: descriptor + caps + profile host green.
- QEMU: zero-controller topology (path build-validated, not HW-proven);
  explicit-xHCI probe still `controllers=0` + init-panic history.
- Physical: BLOCKED (AMD B650/Raphael profile + port-reset fixes landed
  without HW completion proof).
- Limitations: timeout returns error without recovery.
- Security: DMA/IOMMU assumptions unqualified.
- Recovery: endpoint reset absent.
- Final: **PARTIAL**.

## Phase 16 — USB HID/Input — PARTIAL / HW-BLOCKED

- Implemented: report-descriptor parser (short/long, IDs, size/count),
  boot + report-protocol keyboard/mouse adapters, rollover rejection,
  signed mouse fields, ID framing, PS/2 worker, host tests green.
- Remaining: mouse registry/worker/event ABI, interface→endpoint binding
  (first-endpoint-global risk), repeat policy, usage contract, detach
  unregister/cancel, live interrupt-IN data, reattach/stale-poll matrix,
  HW keyboard/mouse proof (target reports USB keyboard not working).
- Tests: parser host green.
- QEMU: no live HID (needs controller).
- Physical: FAIL/BLOCKED (not PASS).
- Limitations: parser PASS is not device PASS.
- Security: oversized/short-report rejection in place.
- Recovery: detach cleanup open.
- Final: **PARTIAL**.

## Phase 17 — TTY, PTY and Console — PARTIAL

- Implemented: canonical/raw, echo/output queues, termios-like flags, UTF-8
  cells, ANSI/VT cursor + screen buffer + J/K erase, PTY master/slave,
  dimensions, foreground-group signals (INT/TSTP/QUIT), session/ctty hooks,
  serial + GOP framebuffer console, host `tty_test` green.
- Remaining: user PTY/ioctl/termios ABI, signal-handler interaction, parser
  fuzzing, PTY/session QEMU matrix, SMP safety, HW USB-console proof.
- Tests: host canonical/raw/echo/PTY/ANSI green.
- QEMU: serial→TTY→shell + signal prompt-recovery green.
- Physical: BLOCKED (needs HID).
- Limitations: PTY mostly kernel-internal; `signal()`/`sigaction()` ENOSYS.
- Security: global TTY/session locking review open.
- Recovery: `tty_recover()` banner path exists.
- Final: **PARTIAL**.

## Phase 18 — Shell and Job Control — PARTIAL

- Implemented: bounded lexer/parser/AST, quoting/escaping/comments,
  variables/arithmetic/command-substitution/glob/completion/history, PATH
  resolver, fork/pipe/dup2/openat/execve/wait runner, redirections,
  pipelines, conditionals, background launch + `[job] done` via WNOHANG
  poll, cwd/chdir/getcwd, QEMU external-command + redirection + pipeline
  suites green.
- Remaining: full job table/notifications, stopped/continued semantics,
  terminal handoff, signal-specific statuses, full-pipe backpressure,
  scripting error modes, large-pipeline/SMP stress, HW interactive proof.
- Tests: `shell_test` frontend green.
- QEMU: echo/cat/grep/args/redirection/pipeline/background green.
- Physical: BLOCKED.
- Limitations: prompt-recovery is not POSIX job-control proof.
- Security: env/command-substitution review partial.
- Recovery: job recovery incomplete.
- Final: **PARTIAL**.

## Phase 19 — Base Unix Userland — PARTIAL

- Implemented: bounded native echo/cat/args/grep/true/false/sleep/ls/mkdir/
  rm/rmdir/touch/stat/ln/head/tail/wc/cut/tr/sort/uniq/env/printf/pwd/which/
  kill/ps/uname/du/df/cp/mv/find/xargs/sed/test/tee/basename/dirname/seq/id/
  whoami/date/ping/curl/host/help/hostname + credential/session/auth
  programs, all with fixed exit/errno paths and QEMU suites green (incl.
  xargs nested fork/exec fix + file-utils + extended). `df` via new
  `STATFS 145` (old 137 proposal was a live-ID collision, superseded).
- Remaining: `free`/`dmesg`/`mount`/`umount` (need sysinfo/klog/mount ABIs
  at 146/147/165/166), GPT/partition tools, HW/storage diagnostics,
  full-disk/read-only/busy-mount matrix.
- Tests: shell/libc helper + new `statfs_test` green.
- QEMU: named-scenario + new `df` suites green.
- Physical: BLOCKED.
- Limitations: bounded options; no synthetic values (honest ENOSYS).
- Security: unprivileged paths return exact errors.
- Recovery: mount/media recovery absent.
- Final: **PARTIAL**.

## Phase 20 — Single-User Credentials and Security — PARTIAL / HARDENING REQUIRED

- Implemented: UID/GID/supp-groups/saved-IDs, owner/group/other + ACL v1,
  setid transitions + env sanitization, capabilities + delegation/drop,
  audit UID, sessions/ctty ownership, `/etc/passwd` + `rixsha256` shadow,
  account add/lock/unlock/rotate/remove + login/logout, chown/chmod policy,
  12-case crash matrix + QEMU cred/session/auth suites green.
- Remaining: complete auth matrix, per-syscall privilege/error contracts,
  `access()` vs kernel evaluator unification, dirfd semantics, errno
  precision, IOMMU-backed DMA authorization, secret hardening, service
  isolation, W^X/ASLR/stack review, TOCTOU/symlink audit, HW + power-loss
  qualification, interactive login manager.
- Tests: host credential/ACL/account partial; QEMU bounded matrix green.
- QEMU: credential/session/account green for covered policy.
- Physical: BLOCKED.
- Limitations: single-user product; no LDAP/AD (out of scope by design).
- Security: review open per above; no weakened permissions.
- Recovery: account-store crash recovery green (12 cases); broader
  filesystem power-loss open.
- Final: **PARTIAL**.

## Phase 21 — Networking — PARTIAL (bounded IPv4/QEMU software scope; HW + TCP semantics open)

- Implemented: Ethernet/ARP/IPv4/ICMP/UDP, bounded in-order TCP (SYN/SYN-ACK/
  ACK, ACK/PSH, checksum/seq validation), sockets + loopback HTTP, routing,
  DHCP/DNS clients, E1000 descriptor/link/TX-complete/RX-consume + net-device
  adapter (QEMU user-net 10.0.2.15/24), RTL8125 register/ring source, host
  `net_test`/`e1000_test`/`rtl8125_test` + QEMU ping/curl/external-fail-closed
  green.
- Remaining: timer-backed retransmit/RTO/backoff, duplicate-ACK/congestion,
  out-of-order reassembly, dynamic windows (fixed 4096B today), true blocking
  waits + readiness/poll, listen/accept/shutdown server, ARP-pending queue
  (single slot today), DHCP renewal, DNS retry/cache, NIC reset/recovery,
  SMP traffic, IPv6 wire proof, HW RTL8125 `10EC:8125` link/TX/RX/IRQ/reset +
  pcap.
- Tests: packet/route/socket/descriptor host green.
- QEMU: loopback + fail-closed external green; loss/reorder absent.
- Physical: BLOCKED.
- Limitations: loss/reorder stalls; polling behavior.
- Security: no-IOMMU DMA risk; packet validation present.
- Recovery: NIC/TCP recovery open.
- Final: **PARTIAL**.

## Phase 22 — Native libc Surface — PARTIAL (bounded static scope; no dynamic claim)

- Implemented: headers/types/errno, strings/memory/stdio, brk-backed
  allocator now with freelist reclaim + split + EINVAL-safe invalid/double
  free/realloc (this slice), filesystem/process/time/socket/signal-mask
  wrappers, localhost mutex/once, locale/UTF-8 basics, compat matrix
  (`docs/PHASE22_COMPAT.md`), 27-group QEMU POSIX suite green.
- Remaining: dynamic loader/TLS, full pthread/futex, signal delivery frames,
  blocking sockets/poll, useful ioctl, file-backed mmap/mprotect/munmap, stat
  gaps, locales/timezones, HW qualification, allocator exhaustion/soak +
  SMP/thread semantics.
- Tests: `libc_test` (now incl. reuse/negative) green; QEMU posix green
  (historical HEAD; rerun open for this slice's allocator).
- QEMU: static scope green.
- Physical: UNVERIFIED.
- Limitations: pthread/signal/mmap/poll intentionally ENOSYS and documented;
  `WIFEXITED` always true reflects kernel status-word reality (no signal
  encoding yet).
- Security: kernel stays freestanding; no libc in kernel.
- Recovery: allocator failures return ENOMEM without corruption.
- Final: **PARTIAL**.

## Final gate

```text
[ ] Phase 00 PASS
[ ] Phase 01 PASS
[ ] Phase 02 PASS
[ ] Phase 03 PASS
[ ] Phase 04 PASS
[ ] Phase 05 PASS
[ ] Phase 06 PASS
[ ] Phase 07 PASS
[ ] Phase 08 PASS
[ ] Phase 09 PASS
[ ] Phase 10 PASS
[ ] Phase 11 PASS
[ ] Phase 12 PASS
[ ] Phase 13 PASS
[ ] Phase 14 PASS
[ ] Phase 15 PASS
[ ] Phase 16 PASS
[ ] Phase 17 PASS
[ ] Phase 18 PASS
[ ] Phase 19 PASS
[ ] Phase 20 PASS
[ ] Phase 21 PASS
[ ] Phase 22 PASS
```

Additional acceptance (`clean build` ✓ for host+image in this slice;
`strict warnings` ✓; `host suite` ✓ incl. new tests; `negative` ✓ for
covered paths; `QEMU UP` ✓ fast subset; `QEMU SMP4` partial (SMP2 proven,
SMP4 spot-check historical); memory/scheduler/IPC/FS/storage/network/USB/
security/reboot/poweroff/recovery/HW/repro-image/docs-sync: **open**).

**Phase 23 stays LOCKED.** No dynamic ELF/shared/TLS/pthreads/Firefox/GUI
work was started. If a later phase needs an earlier prerequisite (e.g. clone
for threads), only the minimum documented slice may land with its own harness.
