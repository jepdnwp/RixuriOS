#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build/uefi"
ESP="$BUILD/esp"
EFI="$ESP/EFI/BOOT"

rm -rf "$BUILD"
mkdir -p "$EFI"

x86_64-w64-mingw32-gcc \
  -std=c17 -ffreestanding -fno-stack-protector -fno-pie -mno-red-zone -m64 \
  -Wall -Wextra -Werror -O2 -c "$ROOT/boot/efi_main.c" -o "$BUILD/efi_main.o"

x86_64-w64-mingw32-gcc \
  -nostdlib -Wl,--subsystem,10 -Wl,-e,efi_main \
  -o "$EFI/BOOTX64.EFI" "$BUILD/efi_main.o"

cp "$ROOT/build/kernel.elf" "$ESP/kernel.elf"

# Reproducible release mode: SOURCE_DATE_EPOCH=<unix-seconds> normalizes
# file mtimes, the FAT volume ID, and all directory-entry timestamps so two
# clean builds of the same revision produce byte-identical esp.img.
if [ -n "${SOURCE_DATE_EPOCH:-}" ]; then
  touch -d "@${SOURCE_DATE_EPOCH}" "$EFI/BOOTX64.EFI" "$ESP/kernel.elf"
fi

# Keep a simple FAT ESP for local QEMU runs and CI artifact inspection.
IMG="$BUILD/esp.img"
truncate -s 64M "$IMG"
if [ -n "${SOURCE_DATE_EPOCH:-}" ]; then
  VOLID="$(printf '%08X' "$((SOURCE_DATE_EPOCH & 0xFFFFFFFF))")"
  mkfs.fat -F 32 -i "$VOLID" "$IMG" >/dev/null
else
  mkfs.fat -F 32 "$IMG" >/dev/null
fi
mmd -i "$IMG" ::/EFI ::/EFI/BOOT
mcopy -i "$IMG" "$EFI/BOOTX64.EFI" ::/EFI/BOOT/BOOTX64.EFI
mcopy -i "$IMG" "$ESP/kernel.elf" ::/kernel.elf
if [ -n "${SOURCE_DATE_EPOCH:-}" ]; then
  python3 "$ROOT/scripts/normalize-fat.py" --image "$IMG" --epoch "$SOURCE_DATE_EPOCH"
fi

printf 'UEFI image: %s\nDirectory ESP: %s\n' "$IMG" "$ESP"
