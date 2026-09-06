#!/usr/bin/env python3
import os
import select
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
IMAGE = ROOT / "build" / "rixfs-ln.img"
ESP = ROOT / "build" / "uefi" / "esp-ln"
LOG = ROOT / "build" / "qemu-ln.log"
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
    command(b"/bin/touch /usr/ln-source")
    command(b"/bin/ln /usr/ln-source /usr/ln-alias")
    command(b"/bin/stat /usr/ln-source")
    command(b"/bin/stat /usr/ln-alias")
    command(b"/bin/rm /usr/ln-source")
    command(b"/bin/stat /usr/ln-alias")
    command(b"/bin/ln /usr /usr/ln-dir")
    command(b"/bin/ln /missing /usr/ln-missing")
    command(b"/bin/rm /usr/ln-alias")
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
if output.count(b"type 2") < 3:
    raise SystemExit("hard-link stat observations missing")
if b"ln: failed" not in output:
    raise SystemExit("hard-link failure cases not observed")
if b"CPU exception" in output or b"PANIC" in output:
    raise SystemExit("kernel fault marker observed")
print("qemu ln test: PASS")
