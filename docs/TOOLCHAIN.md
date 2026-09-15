# RixuriOS Pinned Toolchain (Phase 00)

Validated on Ubuntu 24.04 (WSL) 2026-09-15. All builds use
`CROSS=x86_64-linux-gnu- HOST_CC=gcc` unless noted.

- host gcc: Ubuntu 13.3.0-6ubuntu2~24.04.1 (x86_64-linux-gnu-gcc 13.3.0)
- binutils: GNU ld 2.42 (host + x86_64-linux-gnu)
- UEFI compiler: x86_64-w64-mingw32-gcc 13-win32 (EDK-style `-Wl,--subsystem,10`)
- QEMU: 8.2.2 (Debian 1:8.2.2+ds-0ubuntu1.18), OVMF EDK2 (Ubuntu `ovmf`)
- mtools: 4.0.43 (`mmd`, `mcopy`)
- dosfstools: `mkfs.fat` (FAT32 ESP, 64M)
- xorriso: 1.5.6 (ISO)
- python3: 3.12.3 (image builder + QEMU harnesses)
- make: GNU Make (Ubuntu 24.04)

## Reproducible release mode

```sh
export SOURCE_DATE_EPOCH=$(git log -1 --format=%ct)
make clean
make all CROSS=x86_64-linux-gnu- HOST_CC=gcc
make image CROSS=x86_64-linux-gnu-
```

`build/build_id.h` then embeds `<short-hash>[-dirty]-<UTC stamp from epoch>`
instead of wall-clock time. A dirty tree keeps the `-dirty` suffix so it can
never be mistaken for a clean release artifact. Two clean builds of the same
revision with the same `SOURCE_DATE_EPOCH` produce identical
`build/kernel.elf`, `build/uefi/esp.img`, and `build/RixuriOS.iso` bytes
(proven 2026-09-15: `REPRO_ALL_PASS` over two full clean builds).

How ESP/ISO determinism works:

- `scripts/build-uefi.sh`: file mtimes touched to the epoch, FAT volume ID
  set from the epoch (`mkfs.fat -i`), and every directory-entry timestamp
  normalized by `scripts/normalize-fat.py`.
- `scripts/build-iso.sh`: stage file mtimes touched to the epoch; volume
  timestamps follow `SOURCE_DATE_EPOCH` inside xorriso.
- Without `SOURCE_DATE_EPOCH` the build keeps wall-clock behavior for local
  development; only the epoch mode is claimed reproducible.

Manual probe: `bash scripts/repro-twice.sh [epoch]` (defaults to 1700000000).

## Verification (no CI by owner decision)

There is no hosted CI workflow in this tree. Verification is local and
explicit:

- `make test CROSS=x86_64-linux-gnu- HOST_CC=gcc` (strict host suite)
- `make image` + `make iso` + `make iso-test` (artifact chain)
- Fast QEMU subset: `qemu_pipe_stress_test.py`, `qemu_crash_test.py`,
  `qemu_fuzz_test.py`
- Repro: `bash scripts/repro-twice.sh [epoch]` (three-artifact identity)
- Full matrix: `bash scripts/run-all-tests.sh` (QEMU-heavy, ~30 min, includes
  the skip-enforcement step)
- Release gates: `docs/RELEASE_BLOCKERS.md` (all open; no release claimed)
- Provenance: `make provenance` writes `build/provenance.json`
- ABI check: `make abi-check` verifies `docs/ABI_REGISTRY.md` against
  `kernel/syscall/syscall.h` (any drift fails the build)
