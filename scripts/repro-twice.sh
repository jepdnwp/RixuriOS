#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
EPOCH="${1:-1700000000}"
export SOURCE_DATE_EPOCH="$EPOCH"
make clean >/dev/null
make all CROSS=x86_64-linux-gnu- HOST_CC=gcc >/dev/null 2>&1
make image CROSS=x86_64-linux-gnu- >/dev/null 2>&1
make iso CROSS=x86_64-linux-gnu- >/dev/null 2>&1
sha256sum build/kernel.elf build/uefi/esp.img build/RixuriOS.iso > /tmp/repro_a.txt
cat /tmp/repro_a.txt
make clean >/dev/null
make all CROSS=x86_64-linux-gnu- HOST_CC=gcc >/dev/null 2>&1
make image CROSS=x86_64-linux-gnu- >/dev/null 2>&1
make iso CROSS=x86_64-linux-gnu- >/dev/null 2>&1
sha256sum build/kernel.elf build/uefi/esp.img build/RixuriOS.iso > /tmp/repro_b.txt
cat /tmp/repro_b.txt
if diff /tmp/repro_a.txt /tmp/repro_b.txt; then
  echo REPRO_ALL_PASS
else
  echo REPRO_DIFF
fi
