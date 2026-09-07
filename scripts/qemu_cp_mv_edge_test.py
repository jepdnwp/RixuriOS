#!/usr/bin/env python3
import os
import select
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LOG = ROOT / "build" / "qemu-cp-mv-edge.log"
IMAGE = ROOT / "build" / "rixfs-cp-mv-edge.img"
ESP = ROOT / "build" / "uefi" / "esp-cp-mv-edge"
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
        b"/usr/bin/metatest init",
        b"/usr/bin/metatest policy",
        b"/usr/bin/renametest",
        b"/bin/cp /usr/meta-source /usr/meta-copy",
        b"/usr/bin/metatest check /usr/meta-copy cp-metadata-pass",
        b"/bin/mv /usr/meta-source /usr/meta-moved",
        b"/usr/bin/metatest check-mv /usr/meta-moved mv-metadata-pass",
        b"/bin/rm /usr/meta-copy",
        b"/bin/rm /usr/meta-moved",
        b"/bin/rm /usr/meta-policy",
        b"/usr/bin/metatest init",
        b"/bin/true > /usr/meta-existing-target",
        b"/bin/mv /usr/meta-source /usr/meta-existing-target",
        b"/usr/bin/metatest check-mv /usr/meta-existing-target fallback-metadata-pass",
        b"/bin/rm /usr/meta-existing-target",
        b"/bin/true > /usr/empty-source",
        b"/bin/cp /usr/empty-source /usr/empty-copy",
        b"/bin/ls /usr",
        b"/bin/mv /usr/empty-copy /usr/empty-moved",
        b"/bin/ls /usr",
        b"/bin/rm /usr/empty-moved",
        b"/bin/rm /usr/empty-source",
        b"/bin/true > /usr/overwrite-target",
        b"/bin/cp /bin/echo /usr/overwrite-target",
        b"/usr/overwrite-target overwrite-pass",
        b"/bin/cp /bin/true /usr/mv-source",
        b"/bin/cp /bin/echo /usr/mv-target",
        b"/bin/mv /usr/mv-source /usr/mv-target",
        b"/usr/mv-target mv-overwrite-pass",
        b"/bin/ls /usr",
        b"/bin/rm /usr/overwrite-target",
        b"/bin/rm /usr/mv-target",
        b"/bin/cp /bin/echo /usr/multi-sector-copy",
        b"/usr/multi-sector-copy multi-sector-pass",
        b"/bin/rm /usr/multi-sector-copy",
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
if b"cp: " in output or b"mv: " in output:
    raise SystemExit("cp/mv runtime failure observed")
if b"command execution failed" in output or b"open failed" in output:
    raise SystemExit("shell command execution failure observed")
for marker in (b"metadata-source=PASS", b"chown-policy=PASS", b"rename-inode=PASS", b"rename-overwrite=PASS", b"rename-crossdir=PASS", b"rename-roundtrip=PASS",
               b"cp-metadata-pass", b"mv-metadata-pass", b"fallback-metadata-pass",
               b"overwrite-pass", b"mv-overwrite-pass", b"multi-sector-pass"):
    if marker not in output:
        raise SystemExit(f"missing runtime marker: {marker!r}")
if b"empty-copy" not in output or b"empty-moved" not in output:
    raise SystemExit("empty-file copy/move listing evidence missing")
print("qemu cp/mv edge tests: PASS")
