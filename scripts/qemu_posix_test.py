#!/usr/bin/env python3
"""Phase 22 POSIX surface regression: runs /usr/bin/posix-test on the
disposable NVMe-backed image and requires every group verdict plus the
final posix=PASS marker. Rejects CPU exceptions, page faults and panics.

Prompt discipline: the idle prompt is consumed BEFORE the command line is
sent, so the post-command prompt wait can never match a stale prompt.
"""
import os
import select
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
IMAGE = ROOT / "build" / "rixfs-posix.img"
ESP = ROOT / "build" / "uefi" / "esp-posix"
LOG = ROOT / "build" / "qemu-posix.log"
PROMPT = b"\x1b[1;37m:\x1b[0m "
shutil.copyfile(ROOT / "build" / "rixfs.img", IMAGE)
shutil.copytree(ROOT / "build" / "uefi" / "esp", ESP)
env = os.environ.copy()
env["RIXURI_RIXFS_IMAGE"] = str(IMAGE)
env["RIXURI_ESP"] = str(ESP)
proc = subprocess.Popen(["bash", "./scripts/run-qemu.sh"], cwd=ROOT,
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


def send_line(line: bytes) -> None:
    for byte in line + b"\n":
        proc.stdin.write(bytes((byte,)))
        proc.stdin.flush()
        time.sleep(0.01)


try:
    if not read_until(b"RIXURI: SHELL READY", 30.0):
        raise RuntimeError("embedded init completion not observed")
    time.sleep(1.0)
    if not read_until(PROMPT, 25.0):
        raise RuntimeError("idle prompt not observed before posix-test")
    send_line(b"/usr/bin/posix-test")
    if not read_until(b"posix=", 60.0):
        raise RuntimeError("posix verdict not observed")
    if not read_until(PROMPT, 25.0):
        raise RuntimeError("prompt not observed after posix-test")
    drain_output()
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
for group in (b"clock", b"clock-bad", b"nanosleep", b"gettimeofday", b"sysconf",
              b"mmap", b"mmap-bad", b"munmap", b"mprotect", b"poll", b"ioctl",
              b"signal", b"sigaction", b"sigpending", b"socket-domain",
              b"socket-raw", b"fstat", b"lstat", b"listen", b"setsockopt",
              b"udp-loopback", b"udp-empty", b"exit-atexit", b"sscanf",
              b"getopt-run", b"inet", b"pthread"):
    if b"posix:" + group + b"=PASS" not in output:
        raise SystemExit(f"posix group {group.decode()} not observed")
if b"posix=FAIL" in output:
    raise SystemExit("posix group failure observed")
if b"posix=PASS" not in output:
    raise SystemExit("posix=PASS not observed")
if b"CPU exception" in output or b"PAGE FAULT" in output or b"PANIC" in output:
    raise SystemExit("kernel fault marker observed")
print("qemu posix test: PASS")
