#!/usr/bin/env python3
import os
import select
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LOG = ROOT / "build" / "qemu-file-utils.log"
proc = subprocess.Popen(
    ["bash", "./scripts/run-qemu.sh"],
    cwd=ROOT,
    stdin=subprocess.PIPE,
    stdout=subprocess.PIPE,
    stderr=subprocess.STDOUT,
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
    return False


def command(line: bytes) -> None:
    for byte in line + b"\n":
        proc.stdin.write(bytes((byte,)))
        proc.stdin.flush()
        time.sleep(0.01)
    if not read_until(b"rixuri$ ", 30.0):
        raise RuntimeError(f"prompt not observed after {line!r}")


try:
    if not read_until(b"USER: init returned to kernel", 30.0):
        raise RuntimeError("embedded init completion not observed")
    time.sleep(1.0)
    commands = [
        b"/bin/cp /bin/echo /usr/echo-copy",
        b"/bin/ls /usr",
        b"/bin/mv /usr/echo-copy /usr/echo-moved",
        b"/bin/ls /usr",
        b"/bin/rm /usr/echo-moved",
        b"/bin/ls /usr",
        b"/bin/mkdir /usr/emptydir",
        b"/bin/rmdir /usr/emptydir",
        b"/bin/mkdir /usr/nonempty",
        b"/bin/mkdir /usr/nonempty/child",
        b"/bin/rmdir /usr/nonempty",
        b"/bin/rmdir /usr/nonempty/child",
        b"/bin/rmdir /usr/nonempty",
        ]
    for line in commands:
        command(line)
finally:
    proc.terminate()
    try:
        proc.wait(timeout=3)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()

LOG.write_bytes(output)
sys.stdout.buffer.write(output)
if b"cp: read failed" in output or b"cp: write failed" in output:
    raise SystemExit("cp runtime failure observed")
if b"mv: read failed" in output or b"mv: write failed" in output:
    raise SystemExit("mv runtime failure observed")
if b"rmdir: failed" not in output:
    raise SystemExit("non-empty rmdir rejection was not observed")
if b"command execution failed" in output:
    raise SystemExit("shell command execution failure observed")
print("qemu file utilities test: PASS")
