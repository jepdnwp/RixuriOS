#!/usr/bin/env python3
import os
import select
import shutil
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
IMAGE = ROOT / "build" / "rixfs-curl.img"
ESP = ROOT / "build" / "uefi" / "esp-curl"
shutil.copyfile(ROOT / "build" / "rixfs.img", IMAGE)
shutil.copytree(ROOT / "build" / "uefi" / "esp", ESP)
env = os.environ.copy()
env["RIXURI_RIXFS_IMAGE"] = str(IMAGE)
env["RIXURI_ESP"] = str(ESP)
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

try:
    if not until(b"RIXURI: SHELL READY", 30):
        raise RuntimeError("shell boot marker missing")
    proc.stdin.write(b"/usr/bin/curl\n")
    proc.stdin.flush()
    if not until(b"curl: HTTP 200 loopback PASS", 20):
        raise RuntimeError("curl PASS marker missing")
    if b"<html><body><h1>Hello RixuriOS</h1></body></html>" not in out:
        raise RuntimeError("HTML body missing")
    if not until(b"\x1b[1;37m:\x1b[0m ", 10):
        raise RuntimeError("prompt missing after curl")
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
print("qemu curl test: PASS")
