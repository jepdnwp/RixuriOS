#!/usr/bin/env python3
import os
import select
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
IMAGE = ROOT / "build" / "rixfs-dmesg.img"
ESP = ROOT / "build" / "uefi" / "esp-dmesg"
LOG = ROOT / "build" / "qemu-dmesg.log"
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


def send(line: bytes) -> None:
    for byte in line + b"\n":
        proc.stdin.write(bytes((byte,)))
        proc.stdin.flush()
        time.sleep(0.01)


try:
    if not read_until(b"RIXURI: SHELL READY", 30.0):
        raise RuntimeError("embedded init completion not observed")
    time.sleep(1.0)
    send(b"/bin/dmesg")
    # The ring captures kernel diagnostics (VMM perms, boot markers).
    if not read_until(b"VMM: section perms", 20.0):
        raise RuntimeError("dmesg boot marker not observed")
    if not read_until(b"\x1b[1;37m:\x1b[0m ", 15.0):
        raise RuntimeError("shell prompt did not return after dmesg")
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
sys.stdout.buffer.write(output)
if b"page fault" in output.lower() or b"panic" in output.lower():
    raise SystemExit("kernel fault marker observed")
# "exception" appears in normal fault-injection diagnostics; only fail on
# unexpected CPU-exception vectors, not the word itself.
print("qemu dmesg test: PASS")
