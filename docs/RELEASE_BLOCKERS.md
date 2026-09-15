# RixuriOS Release Blockers (Phase 00)

No release artifact (`kernel.elf`, `esp.img`, `RixuriOS.iso`) may be tagged as
a release while any item below is open. The owner decides releases; this file
is the checklist, `docs/PHASE_00_22_CLOSURE_REPORT.md` is the evidence.

## Blocking gates

1. Every Phase 00–22 gate in the closure report is `PASS` (no `PARTIAL`,
   `HARDENING REQUIRED`, `BROKEN`, `BLOCKED`, or `UNVERIFIED`).
2. `make test CROSS=x86_64-linux-gnu- HOST_CC=gcc` green with
   `-Wall -Wextra -Werror`, including `make abi-check` (81 syscalls, no drift).
3. `bash scripts/run-all-tests.sh` green, including the skip-enforcement step
   (no `SKIP` in the matrix log).
4. `bash scripts/repro-twice.sh` → `REPRO_ALL_PASS` for `kernel.elf`,
   `esp.img`, and `RixuriOS.iso` at the release revision and epoch.
5. `make provenance` manifest archived alongside the artifacts (revision,
   dirty=false, toolchain, hashes).
6. Physical-hardware acceptance for every hardware phase (boot, SMP, NVMe,
   xHCI/HID, NIC, power) archived with raw serial/pcap/hash evidence.
7. No known P0 correctness defect (memory ownership, DMA isolation, storage
   durability, fault containment) and no failing or muted test.
8. Documentation matches the tree: roadmap, closure report, validation log,
   ABI registry, toolchain pins.

## Non-blocking (tracked, honest limitations)

- Bounded static-libc scope exclusions listed in `docs/PHASE22_COMPAT.md`.
- Deferred later-phase work (dynamic ELF, threads/futex, GUI) stays in its
  own roadmap phases and never counts as release evidence.

Current status: **all gates open** — see the closure report. No release is
claimed.
