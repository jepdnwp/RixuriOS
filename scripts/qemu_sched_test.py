#!/usr/bin/env python3
import os, select, shutil, subprocess, sys, time
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
IMAGE = ROOT / "build" / "rixfs-sched.img"
ESP = ROOT / "build" / "uefi" / "esp-sched"
LOG = ROOT / "build" / "qemu-sched.log"
shutil.copyfile(ROOT / "build" / "rixfs.img", IMAGE)
shutil.rmtree(ESP, ignore_errors=True)
shutil.copytree(ROOT / "build" / "uefi" / "esp", ESP)
env = os.environ.copy()
env["RIXURI_RIXFS_IMAGE"] = str(IMAGE)
env["RIXURI_ESP"] = str(ESP)
proc = subprocess.Popen(["bash", "./scripts/run-qemu.sh"], cwd=ROOT,
                        stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                        stderr=subprocess.STDOUT, env=env)
output = bytearray()
cursor = 0
def read_until(marker, timeout):
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
def drain_quiet(idle=2.0, cap=15.0):
    deadline = time.monotonic() + cap
    while time.monotonic() < deadline:
        quiet_until = time.monotonic() + idle
        while time.monotonic() < quiet_until:
            ready, _, _ = select.select([proc.stdout], [], [], 0.05)
            if not ready:
                continue
            chunk = os.read(proc.stdout.fileno(), 4096)
            if not chunk:
                return
            output.extend(chunk)
            quiet_until = time.monotonic() + idle
        return
def command(line):
    for byte in line + b"\n":
        proc.stdin.write(bytes((byte,)))
        proc.stdin.flush()
        time.sleep(0.01)
    if not read_until(b"\x1b[1;37m:\x1b[0m ", 60.0):
        raise RuntimeError(f"prompt not observed after {line!r}")
    drain_quiet()
try:
    if not read_until(b"RIXURI: SHELL READY", 30.0):
        raise RuntimeError("embedded init completion not observed")
    # Sync: consume the shell's first interactive prompt before sending
    # anything. Matching it later as a command prompt shifts every
    # attribution by one under load (observed root cause of flaky
    # missing-marker failures).
    if not read_until(b"\x1b[1;37m:\x1b[0m ", 60.0):
        raise RuntimeError("initial shell prompt not observed")
    command(b"/usr/bin/schedtest churn 40")
    command(b"/usr/bin/schedtest fair")
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
for marker in (b"churn=PASS", b"fair=PASS"):
    if marker not in output:
        raise SystemExit(f"missing runtime marker: {marker!r}")
if b"CPU exception" in output or b"PANIC" in output:
    raise SystemExit("kernel fault marker observed")
print("qemu scheduler test: PASS")
