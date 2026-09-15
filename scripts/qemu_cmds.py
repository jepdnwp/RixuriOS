#!/usr/bin/env python3
"""Run shell commands in QEMU and check for a marker. Usage:
qemu_onecmd_test.py "<marker>" <timeout_s> "<command>" ["<command>" ...]"""
import os
import select
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TAG = os.environ.get("RIXURI_TEST_TAG", "onecmd")
IMAGE = ROOT / "build" / f"rixfs-{TAG}.img"
ESP = ROOT / "build" / "uefi" / f"esp-{TAG}"
LOG = ROOT / "build" / f"qemu-{TAG}.log"
shutil.copyfile(ROOT / "build" / "rixfs.img", IMAGE)
shutil.rmtree(ESP, ignore_errors=True)
shutil.copytree(ROOT / "build" / "uefi" / "esp", ESP)
env = os.environ.copy()
env["RIXURI_RIXFS_IMAGE"] = str(IMAGE)
env["RIXURI_ESP"] = str(ESP)
marker = sys.argv[1].encode()
timeout = float(sys.argv[2])
commands = [a.encode() for a in sys.argv[3:]]
proc = subprocess.Popen(
    ["bash", "./scripts/run-qemu.sh"], cwd=ROOT, stdin=subprocess.PIPE,
    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env,
)
output = bytearray()
cursor = 0


def read_until(want: bytes, limit: float) -> bool:
    global cursor
    deadline = time.monotonic() + limit
    while time.monotonic() < deadline:
        index = output.find(want, cursor)
        if index >= 0:
            cursor = index + len(want)
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
    # One command at a time: sync to the idle prompt first, then send each
    # command only after the previous one returned to the prompt. Blind
    # back-to-back sends overflow the bounded TTY input queue under fork/exec
    # load and arrive truncated (observed: eaten command bytes, silent
    # no-op commands).
    if not read_until(b"\x1b[1;37m:\x1b[0m ", 15.0):
        raise RuntimeError("idle shell prompt not observed")
    deadline = time.monotonic() + timeout
    for command in commands:
        send(command)
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise RuntimeError(f"marker {marker!r} not observed")
        if not read_until(b"\x1b[1;37m:\x1b[0m ", remaining):
            raise RuntimeError("shell prompt did not return")
        time.sleep(0.5)
    if marker not in output:
        raise RuntimeError(f"marker {marker!r} not observed")
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
print("qemu onecmd test: PASS")
