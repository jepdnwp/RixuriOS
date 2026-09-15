#!/usr/bin/env python3
"""P3 Phase R3: affinity + migration test. Boots with -smp 2 on a
disposable image copy and watches the two R3 spinners: R3-P is pinned
AP-only via scheduler_set_affinity (every P row must land on ONE cpu),
R3-F floats (must appear on BOTH cpus — work-conserving picks migrate
it). Pin unanimity is the load-bearing verdict: without affinity P
floats like everything else (see the A/B control). SHELL READY proves
the pinned spinner did not wedge the boot."""
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
IMAGE = ROOT / "build" / "rixfs-migrate.img"
ESP = ROOT / "build" / "uefi" / "esp-migrate"
LOG = ROOT / "build" / "qemu-migrate.log"
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
    # Spinner window: 8 tick-paced reports each over ~400 guest ticks.
    # Same 600 s TCG budget shape as the E7 harness. Reports carry the
    # task's migration count (cross-CPU claims), which is deterministic:
    # sampling luck cannot hide a migration (the counter only grows) and
    # cannot fake a pin (a single foreign claim bumps it).
    deadline = time.monotonic() + 600.0
    p_rows = []
    f_rows = []
    while time.monotonic() < deadline:
        ready, _, _ = select.select([proc.stdout], [], [], 0.2)
        if ready:
            chunk = os.read(proc.stdout.fileno(), 4096)
            if not chunk:
                break
            output.extend(chunk)
        text = output.decode("utf-8", "replace")
        p_rows = re.findall(r"SMP: R3 P on cpu=(\d+) mig=(\d+)", text)
        f_rows = re.findall(r"SMP: R3 F on cpu=(\d+) mig=(\d+)", text)
        if len(p_rows) >= 4 and len(f_rows) >= 4:
            break
    if len(p_rows) < 4:
        raise RuntimeError(f"R3-P placement markers incomplete: {len(p_rows)} rows")
    if len(set(c for (c, _m) in p_rows)) != 1:
        raise RuntimeError(f"R3-P not pinned: cpus {sorted(set(c for (c, _m) in p_rows))} "
                           f"(affinity ignored — no-pin signature)")
    if int(p_rows[-1][1]) != 0:
        raise RuntimeError(f"R3-P migrated {p_rows[-1][1]} times despite AP-only pin")
    if len(f_rows) < 4:
        raise RuntimeError(f"R3-F placement markers incomplete: {len(f_rows)} rows")
    if int(f_rows[-1][1]) < 1:
        raise RuntimeError("R3-F never migrated: work-conserving placement dead")
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
print("qemu affinity/migration test: PASS")
