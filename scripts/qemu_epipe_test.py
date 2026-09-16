#!/usr/bin/env python3
import os
import select
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
IMAGE = ROOT / "build" / "rixfs-epipe.img"
ESP = ROOT / "build" / "uefi" / "esp-epipe"
LOG = ROOT / "build" / "qemu-epipe.log"
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


def drain_output(duration: float = 0.25) -> None:
    deadline = time.monotonic() + duration
    while time.monotonic() < deadline:
        ready, _, _ = select.select([proc.stdout], [], [], 0.02)
        if not ready:
            continue
        chunk = os.read(proc.stdout.fileno(), 4096)
        if not chunk:
            return
        output.extend(chunk)


def command(line: bytes) -> None:
    for byte in line + b"\n":
        proc.stdin.write(bytes((byte,)))
        proc.stdin.flush()
        time.sleep(0.01)
    if not read_until(b"\x1b[1;37m:\x1b[0m ", 25.0):
        raise RuntimeError(f"prompt not observed after {line!r}")
    drain_output()


try:
    if not read_until(b"RIXURI: SHELL READY", 30.0):
        raise RuntimeError("embedded init completion not observed")
    time.sleep(1.0)
    command(b"/usr/bin/epipe-test")
    # Early-closing reader must not wedge the writer: seq out-produces head
    # by orders of magnitude, so the writer always observes EPIPE and exits.
    command(b"/usr/bin/seq 1 100000 | /bin/head")
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
if b"epipe-broken=PASS" not in output or b"epipe=PASS" not in output:
    raise SystemExit("EPIPE errno contract not observed")
if b"\n10\n" not in output:
    raise SystemExit("head pipeline output not observed")
if b"CPU exception" in output or b"PANIC" in output:
    raise SystemExit("kernel fault marker observed")
print("qemu epipe test: PASS")
