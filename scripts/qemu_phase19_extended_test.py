#!/usr/bin/env python3
import os
import select
import shutil
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
IMAGE = ROOT / "build" / "rixfs-phase19-extended.img"
ESP = ROOT / "build" / "uefi" / "esp-phase19-extended"

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

def read_until(marker: bytes, timeout: float = 30.0) -> bool:
    global cursor
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        position = output.find(marker, cursor)
        if position >= 0:
            cursor = position + len(marker)
            return True
        ready, _, _ = select.select([proc.stdout], [], [], 0.2)
        if ready:
            chunk = os.read(proc.stdout.fileno(), 4096)
            if not chunk:
                return False
            output.extend(chunk)
    return False

def command(line: bytes) -> None:
    proc.stdin.write(line + b"\n")
    proc.stdin.flush()
    if not read_until(b"\x1b[1;37m:\x1b[0m ", 20.0):
        raise RuntimeError(f"prompt missing after {line!r}")

try:
    if not read_until(b"USER: init returned to kernel"):
        raise RuntimeError("embedded init completion not observed")
    time.sleep(1.0)
    for line in (
        b"mkdir /p19ext",
        b"cd /p19ext",
        b"pwd",
        b"/bin/echo relative > file",
        b"stat file",
        b"cd /does/not/exist",
        b"pwd",
        b"/usr/bin/seq 1 3 | /usr/bin/tee seq.txt",
        b"/usr/bin/basename /p19ext/seq.txt",
        b"/usr/bin/dirname /p19ext/seq.txt",
        b"/bin/date",
        b"/usr/bin/id",
        b"/usr/bin/whoami",
        b"rmdir /p19ext",
        b"/bin/rm file",
        b"/bin/rm seq.txt",
        b"cd /",
        b"rmdir /p19ext",
    ):
        command(line)
finally:
    proc.terminate()
    try:
        proc.wait(timeout=3)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()
    (ROOT / "build" / "qemu-phase19-extended.log").write_bytes(output)
    IMAGE.unlink(missing_ok=True)
    shutil.rmtree(ESP, ignore_errors=True)

text = output.decode("utf-8", "replace")
required = (b"/p19ext", b"seq.txt", b"uid=0 gid=0\n", b"root\n",
            b"cd: no such directory", b"rmdir: failed",
            b"\x1b[1;32mroot\x1b[0m@\x1b[1;34mrixurios\x1b[0m",
            b"\x1b[1;36m/p19ext\x1b[0m",
            b"\x1b[1;37m:\x1b[0m ")
if any(marker not in output for marker in required):
    raise RuntimeError("required positive/negative evidence missing")
if any(marker in output for marker in (b"PAGE FAULT", b"CPU exception", b"PANIC")):
    raise RuntimeError("kernel fault marker observed")
print("qemu Phase 19 extended cwd/utility/negative test: PASS")
