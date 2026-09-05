#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
make clean
make all
make check
./scripts/build-uefi.sh
./scripts/run-qemu.sh
