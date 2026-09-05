#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ESP="$ROOT/build/uefi/esp"
OVMF_CODE=""

for candidate in \
  /usr/share/OVMF/OVMF_CODE_4M.fd \
  /usr/share/OVMF/OVMF_CODE.fd \
  /usr/share/edk2/ovmf/OVMF_CODE.fd; do
  if [[ -f "$candidate" ]]; then
    OVMF_CODE="$candidate"
    break
  fi
done

if [[ -z "$OVMF_CODE" ]]; then
  echo "OVMF firmware not found" >&2
  exit 1
fi

exec qemu-system-x86_64 \
  -machine q35,accel=tcg \
  -cpu max \
  -m 512M \
  -drive if=pflash,format=raw,readonly=on,file="$OVMF_CODE" \
  -drive format=raw,file=fat:rw:"$ESP" \
  -serial stdio \
  -display none \
  -no-reboot \
  -no-shutdown
