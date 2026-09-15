#!/usr/bin/env python3
"""P3 Phase 06 re-audit: slot-limit pressure test. Boots a disposable UP
image, settles `threads`/`ps` baselines, runs `schedtest burst` (40
concurrent fork+sleep children against 32 task slots — overflow must
fail cleanly), then requires `burst=PASS`, live-thread count back at
baseline (no stale task) and process count back at baseline (no leaked
zombie, incl. create-failed 127s drained via waitpid(-1)). Hardened
prompt/drain discipline throughout."""
import os
import re
import select
import shutil
import signal
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
IMAGE = ROOT / "build" / "rixfs-burst.img"
ESP = ROOT / "build" / "uefi" / "esp-burst"
LOG = ROOT / "build" / "qemu-burst.log"
shutil.copyfile(ROOT / "build" / "rixfs.img", IMAGE)
shutil.rmtree(ESP, ignore_errors=True)
shutil.copytree(ROOT / "build" / "uefi" / "esp", ESP)
env = os.environ.copy()
env["RIXURI_RIXFS_IMAGE"] = str(IMAGE)
env["RIXURI_ESP"] = str(ESP)
proc = subprocess.Popen(
    ["bash", "./scripts/run-qemu.sh"], cwd=ROOT,
    stdin=subprocess.PIPE, stdout=subprocess.PIPE,
    stderr=subprocess.STDOUT, env=env)


def _die(signum, frame):
    raise SystemExit(124)


signal.signal(signal.SIGTERM, _die)
signal.signal(signal.SIGINT, _die)
output = bytearray()
cursor = 0
PROMPT = b"\x1b[1;37m:\x1b[0m "


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


def drain_quiet(idle: float = 2.0, cap: float = 15.0) -> None:
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


def send(line: bytes) -> None:
    for byte in line + b"\n":
        proc.stdin.write(bytes((byte,)))
        proc.stdin.flush()
        time.sleep(0.01)


def threads_count() -> int:
    global cursor
    start = len(output)
    send(b"/usr/bin/threads")
    if not read_until(PROMPT, 60.0):
        raise RuntimeError("prompt not observed after threads")
    drain_quiet()
    text = output[start:].decode("utf-8", "replace")
    m = re.findall(r"^(\d+) thread\(s\)\s*$", text, re.M)
    if not m:
        raise RuntimeError("threads count line not observed")
    rows = re.findall(r"^\s*(\d+)\s+(\d+)\s+([AD?])\s*$", text, re.M)
    if int(m[-1]) != len(rows):
        raise RuntimeError("threads count/rows mismatch")
    if any(s == "D" for (_t, _o, s) in rows):
        raise RuntimeError("DETACHED thread lingers")
    return int(m[-1])


def ps_count() -> int:
    start = len(output)
    send(b"/usr/bin/ps")
    if not read_until(PROMPT, 60.0):
        raise RuntimeError("prompt not observed after ps")
    drain_quiet()
    text = output[start:].decode("utf-8", "replace")
    m = re.findall(r"^(\d+) process\(es\)\s*$", text, re.M)
    if not m:
        raise RuntimeError("ps count line not observed")
    return int(m[-1])


try:
    if not read_until(b"RIXURI: SHELL READY", 120.0):
        raise RuntimeError("shell ready not observed")
    if not read_until(PROMPT, 60.0):
        raise RuntimeError("initial shell prompt not observed")
    drain_quiet()
    base_t = None
    for _ in range(3):
        a, b = threads_count(), threads_count()
        if a == b:
            base_t = a
            break
    if base_t is None:
        raise RuntimeError("thread baseline never settled")
    base_p = ps_count()
    send(b"/usr/bin/schedtest burst")
    if not read_until(b"burst=PASS", 300.0):
        raise RuntimeError("burst=PASS not observed (hang? lost exit?)")
    if not read_until(PROMPT, 60.0):
        raise RuntimeError("prompt not observed after burst")
    drain_quiet()
    after_t = None
    for _ in range(3):
        a, b = threads_count(), threads_count()
        if a == b:
            after_t = a
            break
    if after_t is None:
        raise RuntimeError("post-burst threads never settled")
    if after_t != base_t:
        raise RuntimeError(f"stale threads: baseline {base_t}, after {after_t}")
    after_p = ps_count()
    if after_p != base_p:
        raise RuntimeError(f"leaked processes: baseline {base_p}, after {after_p}")
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
if b"CPU exception" in output or b"PAGE FAULT" in output or b"PANIC" in output:
    raise SystemExit("kernel fault marker observed")
print("qemu slot-limit burst test: PASS")
