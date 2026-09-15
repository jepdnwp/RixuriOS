#!/usr/bin/env python3
"""P3 Phase R4: priority + accounting fairness test. Boots a disposable
UP image (default run) and waits for the R4 spinner finals: H (boosted
HIGH) must accumulate at least 2x the run_ticks of N (NORMAL) over
their shared ~200-tick wall window (steady state forces ~4:1 via the
streak cap: 4 high picks per 1 normal). Both finals prove the streak
cap drains normal work (no starvation); SHELL READY proves the 80%
hog did not wedge the boot. Passive observation, no shell input."""
import os
import re
import select
import shutil
import signal
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
IMAGE = ROOT / "build" / "rixfs-fair.img"
ESP = ROOT / "build" / "uefi" / "esp-fair"
LOG = ROOT / "build" / "qemu-fair.log"
shutil.copyfile(ROOT / "build" / "rixfs.img", IMAGE)
shutil.rmtree(ESP, ignore_errors=True)
shutil.copytree(ROOT / "build" / "uefi" / "esp", ESP)
env = os.environ.copy()
env["RIXURI_RIXFS_IMAGE"] = str(IMAGE)
env["RIXURI_ESP"] = str(ESP)
proc = subprocess.Popen(
    ["bash", "./scripts/run-qemu.sh"], cwd=ROOT,
    stdin=subprocess.PIPE, stdout=subprocess.PIPE,
    stderr=subprocess.STDOUT, env=env)


def _die(signum, frame):
    raise SystemExit(124)


signal.signal(signal.SIGTERM, _die)
signal.signal(signal.SIGINT, _die)
output = bytearray()
cursor = 0


def read_until(marker: bytes, timeout: float) -> bool:
    global cursor
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        index = output.find(marker, cursor)
        if index >= 0:
            cursor = index + len(marker)
            return True
        ready, _, _ = select.select([proc.stdout], [], [], 0.2)
        if ready:
            chunk = os.read(proc.stdout.fileno(), 4096)
            if not chunk:
                return False
            output.extend(chunk)
    return False


try:
    if not read_until(b"RIXURI: SHELL READY", 120.0):
        raise RuntimeError("shell ready not observed")
    # R4 window is ~200 wall ticks after boot; the 80% hog stretches it
    # to ~2.5 s. 300 s budget is generous TCG headroom, same shape as
    # the other spinner harnesses.
    deadline = time.monotonic() + 300.0
    h_final = None
    n_final = None
    while time.monotonic() < deadline:
        ready, _, _ = select.select([proc.stdout], [], [], 0.2)
        if ready:
            chunk = os.read(proc.stdout.fileno(), 4096)
            if not chunk:
                break
            output.extend(chunk)
        text = output.decode("utf-8", "replace")
        m = re.findall(r"SMP: R4 H final ticks=(\d+)", text)
        if m:
            h_final = int(m[-1])
        m = re.findall(r"SMP: R4 N final ticks=(\d+)", text)
        if m:
            n_final = int(m[-1])
        if h_final is not None and n_final is not None:
            break
    if h_final is None:
        raise RuntimeError("R4-H final ticks not observed (high task starved?)")
    if n_final is None:
        raise RuntimeError("R4-N final ticks not observed (normal task starved?)")
    if h_final < 2 * n_final:
        raise RuntimeError(f"no priority share: H={h_final} N={n_final} "
                           f"(need H>=2N; no-boost signature is ~1:1)")
finally:
    LOG.write_bytes(output)
    proc.terminate()
    try:
        proc.wait(timeout=3)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()
    IMAGE.unlink(missing_ok=True)
    shutil.rmtree(ESP, ignore_errors=True)

LOG.write_bytes(output)
sys.stdout.buffer.write(output)
if b"CPU exception" in output or b"PAGE FAULT" in output or b"PANIC" in output:
    raise SystemExit("kernel fault marker observed")
print(f"qemu fairness test: PASS (H={h_final} N={n_final})")
