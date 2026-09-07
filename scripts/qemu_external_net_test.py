#!/usr/bin/env python3
import os
import select
import shutil
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
IMAGE = ROOT / "build" / "rixfs-external-net.img"
ESP = ROOT / "build" / "uefi" / "esp-external-net"
shutil.copyfile(ROOT / "build" / "rixfs.img", IMAGE)
shutil.copytree(ROOT / "build" / "uefi" / "esp", ESP)
env = os.environ.copy()
env["RIXURI_RIXFS_IMAGE"] = str(IMAGE)
env["RIXURI_ESP"] = str(ESP)
env["RIXURI_QEMU_NET"] = "1"
proc = subprocess.Popen(["bash", "./scripts/run-qemu.sh"], cwd=ROOT,
                        stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                        stderr=subprocess.STDOUT, env=env)
out = bytearray()

def until(marker: bytes, timeout: float) -> bool:
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if marker in out:
            return True
        ready, _, _ = select.select([proc.stdout], [], [], 0.2)
        if ready:
            chunk = os.read(proc.stdout.fileno(), 4096)
            if not chunk:
                return False
            out.extend(chunk)
    return marker in out

def command(line: bytes) -> None:
    for byte in line + b"\n":
        proc.stdin.write(bytes((byte,)))
        proc.stdin.flush()
        time.sleep(0.01)
    if not until(b"\x1b[1;37m:\x1b[0m ", 20):
        raise RuntimeError("prompt missing after command")

try:
    if not until(b"RIXURI: SHELL READY", 30):
        raise RuntimeError("shell boot marker missing")
    if not until(b"\x1b[1;37m:\x1b[0m ", 10):
        raise RuntimeError("initial prompt missing")
    time.sleep(1.0)
    command(b"/usr/bin/ping google.com")
    command(b"/usr/bin/curl google.com")
finally:
    proc.terminate()
    try:
        proc.wait(timeout=3)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()
    IMAGE.unlink(missing_ok=True)
    shutil.rmtree(ESP, ignore_errors=True)
if b"CPU exception" in out or b"PAGE FAULT" in out or b"PANIC" in out:
    raise SystemExit("kernel fault marker observed")
print(out.decode("utf-8", "replace"))
if b"ping: DNS/network path unavailable" not in out:
    raise SystemExit("external ping result missing")
if b"curl: DNS/network path unavailable" not in out:
    raise SystemExit("external curl result missing")
print("qemu external network attempt: EXPECTED DNS/stack limitation")
