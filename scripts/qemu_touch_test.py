#!/usr/bin/env python3
import os
import select
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
IMAGE = ROOT / "build" / "rixfs-touch.img"
ESP = ROOT / "build" / "uefi" / "esp-touch"
LOG = ROOT / "build" / "qemu-touch.log"
shutil.copyfile(ROOT / "build" / "rixfs.img", IMAGE)
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


def command(line: bytes) -> None:
    for byte in line + b"\n":
        proc.stdin.write(bytes((byte,)))
        proc.stdin.flush()
        time.sleep(0.01)
    if not read_until(b"rixuri$ ", 25.0):
        raise RuntimeError(f"prompt not observed after {line!r}")


try:
    if not read_until(b"USER: init returned to kernel", 30.0):
        raise RuntimeError("embedded init completion not observed")
    time.sleep(1.0)
    command(b"/bin/touch /usr/touch-created")
    command(b"/bin/ls /usr")
    command(b"/bin/touch /usr/touch-created")
    command(b"/bin/touch /missing/child")
    command(b"/bin/rm /usr/touch-created")
    command(b"/bin/ls /usr")
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
sys.stdout.buffer.write(output)
if b"touch-created" not in output:
    raise SystemExit("created touch file not observed")
if b"touch: failed: /missing/child" not in output:
    raise SystemExit("missing-parent touch failure not observed")
if b"CPU exception" in output or b"PANIC" in output:
    raise SystemExit("kernel fault marker observed")
print("qemu touch test: PASS")
