#!/usr/bin/env bash
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LOG_DIR="$ROOT/build/test-logs"
LOG="$LOG_DIR/all-tests-$(date -u +%Y%m%dT%H%M%SZ).log"
mkdir -p "$LOG_DIR"
PASS=0
FAIL=0

if [[ -t 1 ]]; then
  GREEN=$'\033[1;32m'; RED=$'\033[1;31m'; BLUE=$'\033[1;34m'; RESET=$'\033[0m'
else
  GREEN= RED= BLUE= RESET=
fi

log() { printf '%s\n' "$*" | tee -a "$LOG"; }
run_step() {
  local name="$1"; shift
  log "${BLUE}==> $name${RESET}"
  local start=$SECONDS
  if "$@" 2>&1 | tee -a "$LOG"; then
    PASS=$((PASS + 1))
    log "${GREEN}PASS${RESET} $name ($((SECONDS - start))s)"
  else
    FAIL=$((FAIL + 1))
    log "${RED}FAIL${RESET} $name ($((SECONDS - start))s)"
  fi
}

log "RixuriOS unified test run: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
log "log: $LOG"
run_step "strict build and host suite" make test CROSS=x86_64-linux-gnu- HOST_CC=gcc
run_step "ISO build" make iso CROSS=x86_64-linux-gnu-
run_step "ISO UEFI boot" make iso-test CROSS=x86_64-linux-gnu-
run_step "Phase 20 suite" make phase20-test CROSS=x86_64-linux-gnu-
run_step "power-loss recovery matrix" make powerloss-test CROSS=x86_64-linux-gnu-
for script in "$ROOT"/scripts/qemu_*_test.py; do
  case "$script" in
    *qemu_iso_boot_test.py|*qemu_powerloss_test.py) continue ;;
  esac
  run_step "$(basename "$script")" python3 "$script"
done

log ""
log "RESULT: ${GREEN}$PASS PASS${RESET}, ${RED}$FAIL FAIL${RESET}"
log "FULL LOG: $LOG"
if (( FAIL != 0 )); then exit 1; fi
