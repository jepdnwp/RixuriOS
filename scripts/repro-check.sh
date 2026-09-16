#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
: "${SOURCE_DATE_EPOCH:=1700000000}"
export SOURCE_DATE_EPOCH
export CROSS="${CROSS:-x86_64-linux-gnu-}"
export HOST_CC="${HOST_CC:-gcc}"

cd "$ROOT"
rm -rf build/repro-a build/repro-b build
make clean
make image CROSS="$CROSS" HOST_CC="$HOST_CC"
mkdir -p build/repro-a
cp build/kernel.elf build/rixfs.img build/RixuriOS.iso build/repro-a/
cp -a build/uefi build/repro-a/uefi
make clean
make image CROSS="$CROSS" HOST_CC="$HOST_CC"
mkdir -p build/repro-b
cp build/kernel.elf build/rixfs.img build/RixuriOS.iso build/repro-b/
cp -a build/uefi build/repro-b/uefi

python3 - "$ROOT/build/repro-a" "$ROOT/build/repro-b" <<'PY'
import hashlib
import pathlib
import sys

a, b = map(pathlib.Path, sys.argv[1:])
files = ["kernel.elf", "rixfs.img", "RixuriOS.iso", "uefi/esp.img"]
for rel in files:
    left = (a / rel).read_bytes()
    right = (b / rel).read_bytes()
    if left != right:
        raise SystemExit(f"REPRO_FAIL {rel}: {hashlib.sha256(left).hexdigest()} != {hashlib.sha256(right).hexdigest()}")
    print(f"REPRO_PASS {rel} sha256={hashlib.sha256(left).hexdigest()}")
PY

make provenance CROSS="$CROSS" HOST_CC="$HOST_CC"
echo "REPRO_ALL_PASS epoch=$SOURCE_DATE_EPOCH"
