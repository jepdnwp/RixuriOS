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
revision with the same `SOURCE_DATE_EPOCH` must produce identical
`build/kernel.elf` bytes (modulo absolute build paths in debug sections;
release uses the default `-O2` without `-g`, so paths do not leak).

Remaining non-determinism (explicitly NOT claimed reproducible yet):
FAT ESP timestamps (`mkfs.fat`/`mcopy` embed current time) and ISO volume
timestamps (`xorriso`). Kernel ELF reproducibility is the current gate;
full image/ISO byte-identity requires `libfaketime` or FAT/ISO timestamp
normalisation (tracked, not fabricated).

## CI

`.github/workflows/ci.yml` runs `make test`, `make image`, and the fast QEMU
subset (pipe-stress, crash, fuzz) on every push. Full 37-harness matrix
(`scripts/run-all-tests.sh`) runs nightly/manual (QEMU-heavy, ~30 min).
