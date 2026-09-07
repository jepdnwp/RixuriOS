#!/usr/bin/env python3
import os
import select
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ISO = ROOT / "build" / "RixuriOS.iso"
RIXFS = ROOT / "build" / "rixfs.img"
LOG = ROOT / "build" / "qemu-iso-boot.log"
if not ISO.exists() or not RIXFS.exists():
    raise SystemExit("missing ISO or RixFS image; run make iso first")

env = os.environ.copy()
proc = subprocess.Popen(
    ["qemu-system-x86_64", "-machine", "q35,accel=tcg", "-cpu", "max",
     "-m", "512M", "-drive", "if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd",
     "-cdrom", str(ISO), "-drive", f"if=none,format=raw,file={RIXFS},id=rixfs-test",
     "-device", "nvme,drive=rixfs-test,serial=RIXURI-ISO", "-serial", "stdio",
     "-display", "none", "-no-reboot", "-no-shutdown"],
    cwd=ROOT, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
    stderr=subprocess.STDOUT, env=env)
out = bytearray()
deadline = time.monotonic() + 45.0
try:
    while time.monotonic() < deadline:
        ready, _, _ = select.select([proc.stdout], [], [], 0.25)
        if ready:
            chunk = os.read(proc.stdout.fileno(), 4096)
            if not chunk:
                break
            out.extend(chunk)
            if b"RIXURI:KERNEL_READY" in out and b"shell ready" in out:
                break
finally:
    proc.terminate()
    try:
        proc.wait(timeout=3)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()
LOG.write_bytes(out)
if b"RIXURI:KERNEL_READY" not in out or b"RixuriOS shell ready" not in out:
    raise SystemExit("ISO boot marker missing")
if any(marker in out for marker in (b"PAGE FAULT", b"CPU exception", b"PANIC")):
    raise SystemExit("kernel fault marker observed")
print("qemu ISO UEFI boot: PASS")
