#!/usr/bin/env python3
"""P0 Phase A: SMP discovery boot test. Boots with -smp 4 on a disposable
image copy and requires the discovery line plus a full boot to shell.
APs are recorded, never started: online must stay 1 (BSP only)."""
import os
import select
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
IMAGE = ROOT / "build" / "rixfs-smp.img"
ESP = ROOT / "build" / "uefi" / "esp-smp"
LOG = ROOT / "build" / "qemu-smp.log"
PROMPT = b"\x1b[1;37m:\x1b[0m "
shutil.copyfile(ROOT / "build" / "rixfs.img", IMAGE)
shutil.copytree(ROOT / "build" / "uefi" / "esp", ESP)
env = os.environ.copy()
env["RIXURI_RIXFS_IMAGE"] = str(IMAGE)
env["RIXURI_ESP"] = str(ESP)
proc = subprocess.Popen(
    ["bash", "./scripts/run-qemu.sh", "-smp", "4"], cwd=ROOT,
    stdin=subprocess.PIPE, stdout=subprocess.PIPE,
    stderr=subprocess.STDOUT, env=env)
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
    if not read_until(b"SMP: cpus=4 online=1 bsp_apic=0", 30.0):
        raise RuntimeError("SMP discovery line not observed")
    if not read_until(b"RIXURI:KERNEL_READY", 30.0):
        raise RuntimeError("kernel ready not observed")
    if not read_until(b"RIXURI: SHELL READY", 30.0):
        raise RuntimeError("shell ready not observed")
    if not read_until(PROMPT, 25.0):
        raise RuntimeError("idle prompt not observed")
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
print("qemu smp discovery boot test: PASS")
