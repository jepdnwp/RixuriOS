#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ESP="$ROOT/build/uefi/esp"
OVMF_CODE="${OVMF_CODE:-}"

for candidate in \
  "$OVMF_CODE" \
  /usr/share/OVMF/OVMF_CODE_4M.fd \
  /usr/share/OVMF/OVMF_CODE.fd \
  /usr/share/edk2/ovmf/OVMF_CODE.fd; do
  if [[ -n "$candidate" && -f "$candidate" ]]; then OVMF_CODE="$candidate"; break; fi
done

if [[ -z "$OVMF_CODE" ]]; then echo "OVMF firmware not found" >&2; exit 1; fi
if [[ ! -d "$ESP" ]]; then echo "ESP not found: $ESP. Run 'make image' first." >&2; exit 1; fi

QEMU_ARGS=(
  -machine q35,accel=tcg
  -cpu max
  -m "${RIXURI_QEMU_MEMORY:-512M}"
  -drive "if=pflash,format=raw,readonly=on,file=$OVMF_CODE"
  -drive "format=raw,file=fat:rw:$ESP"
  -serial stdio
  -display none
  -no-reboot
  -no-shutdown
)

exec qemu-system-x86_64 "${QEMU_ARGS[@]}" "$@"
