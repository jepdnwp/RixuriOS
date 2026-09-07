#!/usr/bin/env python3
import os
import select
import shutil
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
IMAGE = ROOT / "build" / "rixfs-ring3.img"
ESP = ROOT / "build" / "uefi" / "esp-ring3"
LOG = ROOT / "build" / "qemu-ring3.log"
shutil.copyfile(ROOT / "build" / "rixfs.img", IMAGE)
shutil.copytree(ROOT / "build" / "uefi" / "esp", ESP)
env = os.environ.copy()
env["RIXURI_RIXFS_IMAGE"] = str(IMAGE)
env["RIXURI_ESP"] = str(ESP)
proc = subprocess.Popen(["bash", "./scripts/run-qemu.sh"], cwd=ROOT,
                        stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                        stderr=subprocess.STDOUT, env=env)
output = bytearray()

def read_until(marker: bytes, timeout: float) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if marker in output:
            return True
        ready, _, _ = select.select([proc.stdout], [], [], 0.2)
        if ready:
            chunk = os.read(proc.stdout.fileno(), 4096)
            if not chunk:
                return False
            output.extend(chunk)
    return marker in output

try:
    required = (b"RIXURI:KERNEL_READY", b"RIXURI:USER_ENTER",
                b"RIXURI:SYSCALL_OK", b"RIXURI: SHELL READY")
    for marker in required:
        if not read_until(marker, 30.0):
            raise RuntimeError(f"missing marker: {marker!r}")
    if b"RIXURI:SYSCALL_FAIL" in output or b"CPU exception" in output or b"PANIC" in output:
        raise RuntimeError("failure marker observed")
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
print("qemu ring3 milestone test: PASS")
