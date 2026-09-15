#!/usr/bin/env python3
"""Full fixture group after a divergent fork prefix (PID/slot aliasing gate).

Runs posix-test + pipetest (advance next_pid past the reused table slots),
then credtest, metatest init/policy, renametest and proc-test. Before the
slot_of() fix, credtest failed at its audit child here while passing bare;
commands with arguments travel intact (no shell quoting involved).
"""
import os
import select
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
IMAGE = ROOT / "build" / "rixfs-fullgroup.img"
ESP = ROOT / "build" / "uefi" / "esp-fullgroup"
LOG = ROOT / "build" / "qemu-fullgroup.log"
COMMANDS = [
    b"/usr/bin/posix-test",
    b"/usr/bin/pipetest",
    b"/usr/bin/credtest",
    b"/usr/bin/metatest init",
    b"/usr/bin/metatest policy",
    b"/usr/bin/renametest",
    b"/usr/bin/proc-test",
]
REQUIRED = [
    b"posix=PASS",
    b"pipe-backpressure=PASS",
    b"cap=PASS",
    b"acl=PASS",
    b"matrix=PASS",
    b"audit=PASS",
    b"setid=PASS",
    b"metadata-source=PASS",
    b"chown-policy=PASS",
    b"rename-roundtrip=PASS",
    b"proc_pipe_wait=PASS",
]
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
    if not read_until(b"\x1b[1;37m:\x1b[0m ", 15.0):
        raise RuntimeError("idle shell prompt not observed")
    deadline = time.monotonic() + 300.0
    for command in COMMANDS:
        send(command)
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise RuntimeError("fullgroup timed out")
        if not read_until(b"\x1b[1;37m:\x1b[0m ", remaining):
            raise RuntimeError("shell prompt did not return")
        time.sleep(0.5)
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
for marker in REQUIRED:
    if marker not in output:
        raise SystemExit(f"missing {marker!r}")
if b"FAIL " in output or b"=FAIL" in output:
    raise SystemExit("FAIL marker observed")
if b"page fault" in output.lower() or b"panic" in output.lower():
    raise SystemExit("kernel fault marker observed")
print("qemu fullgroup test: PASS")
