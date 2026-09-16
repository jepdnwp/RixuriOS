#!/usr/bin/env python3
"""Fast first-prompt smoke: boot a private image and require the shell's
first prompt within 60 s of SHELL READY, with no fault markers."""
import os
import select
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
IMAGE = ROOT / "build" / "rixfs-prompt.img"
ESP = ROOT / "build" / "uefi" / "esp-prompt"
LOG = ROOT / "build" / "qemu-prompt.log"
shutil.copyfile(ROOT / "build" / "rixfs.img", IMAGE)
shutil.rmtree(ESP, ignore_errors=True)
shutil.copytree(ROOT / "build" / "uefi" / "esp", ESP)
env = os.environ.copy()
env["RIXURI_RIXFS_IMAGE"] = str(IMAGE)
env["RIXURI_ESP"] = str(ESP)
proc = subprocess.Popen(
    ["bash", "./scripts/run-qemu.sh"], cwd=ROOT, stdin=subprocess.PIPE,
    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env,
)
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
    if not read_until(b"RIXURI: SHELL READY", 60.0):
        raise RuntimeError("embedded init completion not observed")
    if not read_until(b"\x1b[1;37m:\x1b[0m ", 60.0):
        raise RuntimeError("first shell prompt not observed")
finally:
    proc.terminate()
    try:
        proc.wait(timeout=3)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()
    IMAGE.unlink(missing_ok=True)
    shutil.rmtree(ESP, ignore_errors=True)
    LOG.write_bytes(output)

LOG.write_bytes(output)
if b"PANIC" in output or b"CPU exception" in output:
    raise SystemExit("kernel fault marker observed")
print("qemu prompt test: PASS")
