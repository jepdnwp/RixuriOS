#!/usr/bin/env python3
"""P3 Phase E7: symmetric AP preemption test. Boots with -smp 2 on a
disposable image copy and requires the three never-yielding E7 spinners
to INTERLEAVE on cpu 1 (no near-full group run), plus an APREEMPT
quantum-consumption line. Mere presence does not discriminate (spinners
exit after 8 reports, so even a cooperative AP runs all three
sequentially); interleaving does. Passive observation (no shell
interaction); SHELL READY proves the spinners did not wedge the boot."""
import os
import select
import shutil
import signal
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
IMAGE = ROOT / "build" / "rixfs-smppreempt.img"
ESP = ROOT / "build" / "uefi" / "esp-smppreempt"
LOG = ROOT / "build" / "qemu-smp-preempt.log"
shutil.copyfile(ROOT / "build" / "rixfs.img", IMAGE)
shutil.rmtree(ESP, ignore_errors=True)
shutil.copytree(ROOT / "build" / "uefi" / "esp", ESP)
env = os.environ.copy()
env["RIXURI_RIXFS_IMAGE"] = str(IMAGE)
env["RIXURI_ESP"] = str(ESP)
proc = subprocess.Popen(
    ["bash", "./scripts/run-qemu.sh", "-smp", "2"], cwd=ROOT,
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
    if not read_until(b"SMP: cpus=2", 30.0):
        raise RuntimeError("SMP discovery line not observed")
    if not read_until(b"SMP: online=2", 240.0):
        raise RuntimeError("SMP all-APs-online line not observed")
    if not read_until(b"RIXURI:KERNEL_READY", 60.0):
        raise RuntimeError("kernel ready not observed")
    if b"RIXURI: SHELL READY" not in output:
        if not read_until(b"RIXURI: SHELL READY", 120.0):
            raise RuntimeError("shell ready not observed")
    # APREEMPT first: the trace is capped at 3 lines and they fire early
    # (first quantum consumptions, during boot), while the E7 sharing
    # evidence accumulates later. Searched order-independently: the lines
    # may already sit behind the cursor.
    if b"APREEMPT cpu=1" not in output:
        if not read_until(b"APREEMPT cpu=1", 300.0):
            raise RuntimeError("AP quantum-consumption line not observed")
    # Each spinner must time-share cpu 1 (600 s total budget for slow TCG;
    # spinner reports are guest-tick-paced, 8 per ~4 guest-seconds each).
    # Interleaving is the actual verdict, not mere presence: spinners exit
    # after 8 reports, so even a cooperative AP runs all three
    # SEQUENTIALLY (eight A-on-1 in a row, then B, then C). Preemption
    # round-robins them (observed: A,B,C,A,B,C...). Parse the cpu-1
    # subsequence in order and require all three present with no
    # near-full group run (bound 4 vs cooperative 8; steady-state
    # round-robin yields runs of ~2).
    import re as _re
    deadline = time.monotonic() + 600.0
    seq = []
    while time.monotonic() < deadline:
        ready, _, _ = select.select([proc.stdout], [], [], 0.2)
        if ready:
            chunk = os.read(proc.stdout.fileno(), 4096)
            if not chunk:
                break
            output.extend(chunk)
        text = output.decode("utf-8", "replace")
        seq = _re.findall(r"SMP: E7 ([ABC]) on cpu=1", text)
        if set(seq) >= {"A", "B", "C"}:
            break
    if set(seq) < {"A", "B", "C"}:
        raise RuntimeError(f"AP time-sharing markers incomplete: {sorted(set(seq))} "
                           f"(cooperative-AP signature: spinners missing on cpu 1)")
    longest = 1
    run = 1
    for i in range(1, len(seq)):
        run = run + 1 if seq[i] == seq[i - 1] else 1
        longest = max(longest, run)
    if longest > 4:
        raise RuntimeError(f"cpu-1 observations grouped (longest run {longest}): "
                           f"sequential-AP signature, no time-sharing")
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
print("qemu smp preemption test: PASS")
