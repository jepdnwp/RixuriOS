#!/usr/bin/env python3
import os
import select
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LOG = ROOT / "build" / "qemu-file-utils.log"
IMAGE = ROOT / "build" / "rixfs-file-utils.img"
ESP = ROOT / "build" / "uefi" / "esp-file-utils"
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
    return False


def command(line: bytes) -> None:
    for byte in line + b"\n":
        proc.stdin.write(bytes((byte,)))
        proc.stdin.flush()
        time.sleep(0.01)
    if not read_until(b"\x1b[1;37m:\x1b[0m ", 30.0):
        raise RuntimeError(f"prompt not observed after {line!r}")


try:
    if not read_until(b"RIXURI: SHELL READY", 30.0):
        raise RuntimeError("embedded init completion not observed")
    time.sleep(1.0)
    commands = [
        b"ls usr/bin",
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
        b"/bin/ls -l /bin",
        b"/bin/cat -n /etc/hostname",
        b"/bin/mkdir -p /tmp/deep/nest",
        b"/bin/ls /tmp/deep",
        b"/bin/rmdir /tmp/deep/nest",
        b"/bin/rmdir /tmp/deep",
        b"/usr/bin/hostname",
        b"history",
        b"help ps",
        b"echo -n abc",
        b"uname -a",
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
    IMAGE.unlink(missing_ok=True)
    shutil.rmtree(ESP, ignore_errors=True)

LOG.write_bytes(output)
sys.stdout.buffer.write(output)
if b"cp: read failed" in output or b"cp: write failed" in output:
    raise SystemExit("cp runtime failure observed")
if b"mv: read failed" in output or b"mv: write failed" in output:
    raise SystemExit("mv runtime failure observed")
if b"rmdir: failed" not in output:
    raise SystemExit("non-empty rmdir rejection was not observed")
if b" 0 0 " not in output or b" echo\n" not in output:
    raise SystemExit("ls -l output not observed")
if b"     1\trixurios\n" not in output:
    raise SystemExit("cat -n output not observed")
if b"nest\n" not in output:
    raise SystemExit("mkdir -p output not observed")
if b"rixurios\n" not in output:
    raise SystemExit("hostname output not observed")
if b"/bin/ls /usr\n" not in output:
    raise SystemExit("history output not observed")
if b"ps: list processes" not in output:
    raise SystemExit("help topic output not observed")
if b"abc\x1b[1;32m" not in output:
    raise SystemExit("echo -n output not observed")
if b"x86_64" not in output:
    raise SystemExit("uname -a output not observed")
if b"command execution failed" in output:
    raise SystemExit("shell command execution failure observed")
print("qemu file utilities test: PASS")
