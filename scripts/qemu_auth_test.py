#!/usr/bin/env python3
import os
import select
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
IMAGE = ROOT / "build" / "rixfs-auth.img"
ESP = ROOT / "build" / "uefi" / "esp-auth"
LOG = ROOT / "build" / "qemu-auth.log"
shutil.copyfile(ROOT / "build" / "rixfs.img", IMAGE)
shutil.copytree(ROOT / "build" / "uefi" / "esp", ESP)
env = os.environ.copy()
env["RIXURI_RIXFS_IMAGE"] = str(IMAGE)
env["RIXURI_ESP"] = str(ESP)
proc = subprocess.Popen(
    ["bash", "./scripts/run-qemu.sh"],
    cwd=ROOT,
    stdin=subprocess.PIPE,
    stdout=subprocess.PIPE,
    stderr=subprocess.STDOUT,
    env=env,
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


def command(line: bytes, expected: bytes | None = None) -> None:
    start = cursor
    for byte in line + b"\n":
        proc.stdin.write(bytes((byte,)))
        proc.stdin.flush()
        time.sleep(0.01)
    if not read_until(b"rixuri$ ", 25.0):
        raise RuntimeError(f"prompt not observed after {line!r}")
    if expected is not None and expected not in output[start:]:
        raise RuntimeError(f"expected {expected!r} after {line!r}")


try:
    if not read_until(b"USER: init returned to kernel", 30.0):
        raise RuntimeError("embedded init completion not observed")
    time.sleep(1.0)
    command(b"/usr/bin/authcheck list")
    command(b"/usr/bin/authcheck record operator")
    command(b"/usr/bin/authcheck verify operator phase20-pass")
    command(b"/usr/bin/authcheck verify operator wrong-password")
    command(b"/usr/bin/authcheck protected")
    command(b"/usr/bin/accountctl add auditor 1100 auditor-pass")
    command(b"/usr/bin/authcheck record auditor")
    command(b"/usr/bin/authcheck verify auditor auditor-pass", b"auth-pass")
    command(b"/usr/bin/accountctl lock auditor")
    command(b"/usr/bin/authcheck verify auditor auditor-pass", b"auth-denied")
    command(b"/usr/bin/accountctl unlock auditor")
    command(b"/usr/bin/authcheck verify auditor auditor-pass", b"auth-pass")
    command(b"/usr/bin/accountctl rotate missing missing-pass", b"command execution failed")
    command(b"/usr/bin/authcheck verify operator phase20-pass", b"auth-pass")
    command(b"/usr/bin/accountctl rotate operator phase20-rotated")
    command(b"/usr/bin/authcheck verify operator phase20-rotated")
    command(b"/usr/bin/authcheck login operator phase20-rotated")
    command(b"/usr/bin/authcheck verify operator phase20-pass")
    command(b"/usr/bin/accountctl remove auditor")
    command(b"/usr/bin/authcheck list")
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
for marker in (
    b"accounts=3",
    b"account-record=PASS",
    b"auth-pass",
    b"auth-denied",
    b"shadow-protected=PASS",
    b"account-add=PASS",
    b"account-lock=PASS",
    b"account-unlock=PASS",
    b"password-rotate=PASS",
    b"login=PASS",
    b"account-remove=PASS",
    b"accounts=3",
):
    if marker not in output:
        raise SystemExit(f"missing authentication marker: {marker!r}")
if b"CPU exception" in output or b"PANIC" in output:
    raise SystemExit("kernel fault marker observed")
print("qemu account/authentication test: PASS")
