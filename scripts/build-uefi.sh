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

# Keep a simple FAT ESP for local QEMU runs and CI artifact inspection.
IMG="$BUILD/esp.img"
truncate -s 64M "$IMG"
mkfs.fat -F 32 "$IMG" >/dev/null
mmd -i "$IMG" ::/EFI ::/EFI/BOOT
mcopy -i "$IMG" "$EFI/BOOTX64.EFI" ::/EFI/BOOT/BOOTX64.EFI
mcopy -i "$IMG" "$ESP/kernel.elf" ::/kernel.elf

printf 'UEFI image: %s\nDirectory ESP: %s\n' "$IMG" "$ESP"
