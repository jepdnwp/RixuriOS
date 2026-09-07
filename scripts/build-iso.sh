#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ESP="$ROOT/build/uefi/esp"
ESP_IMAGE="$ROOT/build/uefi/esp.img"
STAGE="$ROOT/build/iso-root"
ISO="$ROOT/build/RixuriOS.iso"
if [[ ! -d "$ESP" || ! -f "$ESP_IMAGE" ]]; then
  echo "ISO prerequisites missing; run 'make image' first." >&2
  exit 1
fi
rm -rf "$STAGE"
mkdir -p "$STAGE"
cp -a "$ESP/." "$STAGE/"
cp "$ESP_IMAGE" "$STAGE/esp.img"
xorriso -as mkisofs \
  -iso-level 3 \
  -V RIXURIOS \
  -o "$ISO" \
  -e esp.img \
  -no-emul-boot \
  "$STAGE" >/dev/null 2>&1
printf 'ISO: %s\n' "$ISO"
