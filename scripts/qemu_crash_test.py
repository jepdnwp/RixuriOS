#!/usr/bin/env python3
"""P3 Phase F2: user-fault kill test. Boots a disposable UP image and
runs `crashtest null` + `crashtest ud`: each forks a child that faults
on purpose (#PF on address 0, #UD). The kernel must terminate just the
child (exit 139, reaped by the parent: `crash-reap=PASS` x2) and keep
running (SHELL READY before/after, prompt sync, no fault markers).
A freeze instead of a kill fails via timeout."""
import os
import select
import shutil
import signal
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
IMAGE = ROOT / "build" / "rixfs-crash.img"
ESP = ROOT / "build" / "uefi" / "esp-crash"
LOG = ROOT / "build" / "qemu-crash.log"
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
PROMPT = b"\x1b[1;37m:\x1b[0m "


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


def drain_quiet(idle: float = 2.0, cap: float = 15.0) -> None:
    deadline = time.monotonic() + cap
    while time.monotonic() < deadline:
        quiet_until = time.monotonic() + idle
        while time.monotonic() < quiet_until:
            ready, _, _ = select.select([proc.stdout], [], [], 0.05)
            if not ready:
                continue
            chunk = os.read(proc.stdout.fileno(), 4096)
            if not chunk:
                return
            output.extend(chunk)
            quiet_until = time.monotonic() + idle
        return


def command(line: bytes, expected: bytes) -> None:
    for byte in line + b"\n":
        proc.stdin.write(bytes((byte,)))
        proc.stdin.flush()
        time.sleep(0.01)
    if not read_until(expected, 120.0):
        raise RuntimeError(f"expected {expected!r} after {line!r}")
    if not read_until(PROMPT, 60.0):
        raise RuntimeError(f"prompt not observed after {line!r}")
    drain_quiet()


try:
    if not read_until(b"RIXURI: SHELL READY", 120.0):
        raise RuntimeError("shell ready not observed")
    if not read_until(PROMPT, 60.0):
        raise RuntimeError("initial shell prompt not observed")
    drain_quiet()
    command(b"/usr/bin/crashtest null", b"crash-reap=PASS")
    command(b"/usr/bin/crashtest ud", b"crash-reap=PASS")
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
print("qemu user-fault kill test: PASS")
