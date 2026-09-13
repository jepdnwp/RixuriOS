# RixuriOS Phase 00–22 Execution Plan

**HEAD:** `ff73d5a` (2026-09-13; local, push pending owner auth)
**Rulebook:** `docs/ROADMAP.md` + roadmap evidence rules. No Phase 23.
**Status language:** `PASS` only per the 12-criterion closure rule.
`QEMU-GREEN` = current-HEAD QEMU evidence, no physical claim.

## 1. Baseline at plan time (all current-HEAD, `CROSS=x86_64-linux-gnu-`)

`make test` RC=0. `make iso` + `iso-test` PASS. Full QEMU matrix PASS:
auth, cp_mv, file_utils, cred, phase20 (session+auth), powerloss,
smp_boot, ring3, ping, curl, ln, stat, touch, head_tail, text_utils,
env_utils, process_utils, posix, phase19_utils (incl. xargs pipeline),
phase19_extended, pipe_stress, signal, session, external_net
(fail-closed), xhci_probe. Triage baseline was 20/28; the F1c compact-
dirent fix closed the fs-corruption failures (xargs, pipe-stress
cross-test, cred `fail-create2`).

## 2. Phase verdicts (re-audit of current HEAD)

| Phase | Verdict | Reason |
|---|---|---|
| 00 | PARTIAL | Build green, no CI/pinning/repro/SBOM. |
| 01 | PARTIAL | QEMU boot ok; no fault-injection harness, no HW boot log. |
| 02 | HARDENING REQUIRED | Reclaim gate not met: `kfree` no-op, no reserved-bit setting, no sync, W^X unenforced (281229d reverted with cause). pmm/heap host tests exist. |
| 03 | PARTIAL / HARDENING | Fail-stop only; no vector matrix, no user-fault recovery. |
| 04 | PARTIAL | smp_boot PASS QEMU; PIT-only, no HPET/APIC-timer, no HW topology. |
| 05 | PARTIAL / HARDENING | No mutex/RW/sem; waitqueue is metadata-only; no lockdep. |
| 06 | BROKEN (gate) | Cooperative scheduler; preemption reverted. Biggest build item. |
| 07 | PARTIAL / HARDENING | No fixup; validate-then-deref; errno gaps; ID registry unversioned. |
| 08 | PARTIAL | Static-only by design (deferred to 23); overlap policy untested. |
| 09 | PARTIAL / HARDENING | No blocking writer, no FIFOs/unix-sockets/FD-passing, SHM lifetime open. |
| 10 | PARTIAL / HARDENING | No IOMMU/DMA-domain ownership; MSI-X lifecycle open. |
| 11 | PARTIAL / HARDENING | Known dirty-evict data-loss bug open; no queues/FUA/retry. |
| 12 | PARTIAL | Polling I/O green; no reset/recovery, 2-page PRP cap, no HW. |
| 13 | PARTIAL / HARDENING | S2 symlinks done; vnode/CLOEXEC/repair/busy-unmount open; legacy `vfs_file.c` diverges. |
| 14 | PARTIAL / HARDENING | RTC UIP unbounded, yield-sleep, no HW reset/S5. |
| 15 | PARTIAL / HW-BLOCKED | Build+host green, H1–H4 in, H5 unvalidated; no HW keyboard. |
| 16 | PARTIAL / HW-BLOCKED | Parsers host-tested; end-to-end needs HW HID. |
| 17 | PARTIAL | Host TTY tests green; sessions/signals partial; no fuzzing. |
| 18 | PARTIAL | Shell+pipelines+jobs basic green; fg/bg/stopped semantics partial. |
| 19 | PARTIAL | Broad utilities green incl. xargs; statfs/sysinfo APIs absent by design (documented). |
| 20 | PARTIAL | Cred/session/auth green QEMU; TOCTOU/symlink-race review, W^X/ASLR open. |
| 21 | PARTIAL | Loopback green, external fail-closed; no HW NIC, no loss/recovery matrix. |
| 22 | PARTIAL (bounded) | Static libc green; allocator reclaim, realloc metadata, wait-status gaps documented. |

No phase is marked PASS: physical evidence and security reviews are
open across the board, and gates (02 reclaim, 06 preemption) are unmet.

## 3. Work order (dependency-first)

1. **P1 — Phase 02 memory:** reserved-bit ownership, reclaimable heap
   + real `kfree`, alloc rollback, PMM/VMM locking, W^X via PS-split
   (re-land 281229d correctly), isolation tests, pressure runs.
2. **P2 — Phase 05 sync:** mutex/RW/sem, scheduler-backed wait queues,
   refcounts, lockdep-lite, stress.
3. **P3 — Phase 06 scheduler:** thread/TID objects, per-CPU runqueues,
   preempt-disable nesting, staged timer preemption, hog/migration/
   affinity tests. Never-yield must not monopolize a CPU.
4. **P4 — Phases 03+07:** fixup-based uaccess, EFAULT, vector matrix,
   malformed-return/nested-fault policy, syscall fuzz, ABI registry.
5. **P5 — Phases 08/09/10/11/12/13/14:** ELF overlap policy + corpus;
   pipe blocking/FIFO/death cleanup; DMA ownership; dirty-evict fix +
   queues; NVMe reset/recovery + PRP lists; vnode/CLOEXEC/repair +
   legacy-VFS audit; bounded RTC + blocking sleep.
6. **P6 — Phases 15–22:** xHCI recovery paths; HW keyboard (owner);
   TTY fuzz; job control; userland gaps; TOCTOU review; TCP
   loss/recovery; libc allocator/wait-status fixes + support matrix.
7. **P0 — build:** pinned toolchain doc, `SOURCE_DATE_EPOCH`
   reproducible mode, CI workflow, skip-enforcement, provenance.

## 4. External blockers (owner / hardware)

- `git push origin main` (credential auth; 4+ local commits ahead).
- HW boot + keyboard evidence on the ASUS target (H5, phases 12/15/16).
- HW NIC, NVMe-qualification, reset/S5, power-loss on media.
- No physical evidence will be fabricated; those items stay
  BLOCKED/UNVERIFIED with harnesses ready.

## 5. Working agreements

Small focused commits (`fix:`/`test:`/`docs:`), `git diff --check`,
`-Wall -Wextra -Werror`, full matrix before phase claims, root-cause
fixes, no deleted/weakened tests, VALIDATION_LOG entries per phase
slice, re-audit before closing anything.
