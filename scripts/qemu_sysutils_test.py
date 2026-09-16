#!/usr/bin/env python3
import os
import select
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
IMAGE = ROOT / "build" / "rixfs-sysutils.img"
ESP = ROOT / "build" / "uefi" / "esp-sysutils"
LOG = ROOT / "build" / "qemu-sysutils.log"
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
    command(b"/bin/ln -s /bin/echo /tmp/rl-link")
    command(b"/bin/readlink /tmp/rl-link")
    command(b"/bin/readlink /bin/echo")
    command(b"/bin/touch /tmp/mode-file")
    command(b"/bin/chmod 600 /tmp/mode-file")
    command(b"/bin/stat /tmp/mode-file")
    command(b"/bin/chmod 644 /tmp/mode-file")
    command(b"/bin/stat /tmp/mode-file")
    command(b"/bin/chmod 999 /tmp/mode-file")
    command(b"/bin/chown 1000:1000 /tmp/mode-file")
    command(b"/bin/stat /tmp/mode-file")
    command(b"/bin/rm /tmp/rl-link")
    command(b"/bin/rm /tmp/mode-file")
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
if b"/bin/echo" not in output:
    raise SystemExit("readlink target not observed")
if b"readlink: failed" not in output:
    raise SystemExit("readlink non-link failure not observed")
if b"mode 33152" not in output:
    raise SystemExit("chmod 600 mode not observed")
if b"mode 33188" not in output:
    raise SystemExit("chmod 644 mode not observed")
if b"expected octal" not in output:
    raise SystemExit("chmod bad-mode rejection not observed")
if b"uid 1000" not in output or b"gid 1000" not in output:
    raise SystemExit("chown ownership not observed")
if b"CPU exception" in output or b"PANIC" in output:
    raise SystemExit("kernel fault marker observed")
print("qemu sysutils test: PASS")
