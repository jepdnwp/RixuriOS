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
DUMP = ROOT / "build" / "phase21-external-net.pcap"
shutil.copyfile(ROOT / "build" / "rixfs.img", IMAGE)
shutil.copytree(ROOT / "build" / "uefi" / "esp", ESP)
DUMP.unlink(missing_ok=True)
env = os.environ.copy()
env["RIXURI_RIXFS_IMAGE"] = str(IMAGE)
env["RIXURI_ESP"] = str(ESP)
env["RIXURI_QEMU_NET"] = "1"
env["RIXURI_QEMU_NET_DUMP"] = str(DUMP)
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

def until_after(marker: bytes, start: int, timeout: float) -> bool:
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if marker in out[start:]:
            return True
        ready, _, _ = select.select([proc.stdout], [], [], 0.2)
        if ready:
            chunk = os.read(proc.stdout.fileno(), 4096)
            if not chunk:
                return False
            out.extend(chunk)
    return marker in out[start:]

def command(line: bytes) -> None:
    start = len(out)
    proc.stdin.write(line + b"\n")
    proc.stdin.flush()
    if not until_after(b"\x1b[1;37m:\x1b[0m ", start, 20):
        raise RuntimeError("prompt missing after command; tail=" +
                           out[start:].decode("utf-8", "replace")[-2000:])

try:
    if not until(b"RIXURI: SHELL READY", 30):
        raise RuntimeError("shell boot marker missing")
    if not until(b"\x1b[1;37m:\x1b[0m ", 10):
        raise RuntimeError("initial prompt missing")
    command(b"/usr/bin/ping google.com")
    command(b"/usr/bin/curl google.com")
    command(b"/usr/bin/curl facebook.com")
    command(b"cat /etc/hosts")
    command(b"cat /etc/hostname")
    command(b"cat /etc/resolv.conf")
    command(b"ls /etc")
    command(b"ls /dev")
    command(b"ls /var")
    command(b"ls /tmp")
    command(b"/usr/bin/host localhost")
    command(b"/usr/bin/host google.com")
    command(b"echo 10.0.2.2 testgateway >> /etc/hosts")
    command(b"/usr/bin/host testgateway")
except Exception:
    (ROOT / "build" / "extnet-fail.log").write_bytes(bytes(out))
    raise
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
if out.count(b"external PASS") < 2:
    raise SystemExit("external curl PASS missing (google.com + facebook.com)")
if b"127.0.0.1 localhost" not in out:
    raise SystemExit("hosts file content missing")
if b"nameserver 10.0.2.3" not in out:
    raise SystemExit("resolv.conf content missing")
if b"localhost has address 127.0.0.1" not in out:
    raise SystemExit("hosts-based resolution missing")
if b"google.com has address " not in out:
    raise SystemExit("DNS resolution via host tool missing")
if b"testgateway has address 10.0.2.2" not in out:
    raise SystemExit("runtime hosts entry missing")
print("qemu external network: PASS")
