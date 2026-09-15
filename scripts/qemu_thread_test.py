#!/usr/bin/env python3
"""P3 Phase R1: thread/TID lifecycle test. Boots a disposable UP image and
drives the new `threads` program (RIX_SYS_LIST_THREADS) across process
spawn/exit load. Verdicts, all harness-side:
  1. every `threads` snapshot parses (header + N rows + count line agree);
  2. TID 1 / OWNER 0 exists (reserved BSP boot thread);
  3. the live count settles (two equal baselines) and returns to baseline
     after load (no thread leak: alloc on create, free on task death);
  4. max TID strictly grows across the load (monotonic, never reused);
  5. no DETACHED row lingers in a settled snapshot (detach is transient;
     reaped owners leave no live thread behind).
No shell interaction beyond fixed commands; SHELL READY + prompt sync
follow the pipe-close harness discipline."""
import os
import re
import select
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
IMAGE = ROOT / "build" / "rixfs-thread.img"
ESP = ROOT / "build" / "uefi" / "esp-thread"
LOG = ROOT / "build" / "qemu-thread.log"
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
ROW = re.compile(r"^\s*(\d+)\s+(\d+)\s+([AD?])\s*$")
COUNT = re.compile(r"^(\d+) thread\(s\)\s*$")


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


def snapshot_threads():
    """Run `threads` and parse its table. Returns (rows, count)."""
    global cursor
    start = len(output)
    command(b"/usr/bin/threads")
    text = output[start:].decode("utf-8", "replace")
    rows = []
    count = None
    seen_header = False
    for line in text.splitlines():
        if "TID OWNER STAT" in line:
            seen_header = True
            continue
        m = ROW.match(line)
        if m:
            rows.append((int(m.group(1)), int(m.group(2)), m.group(3)))
            continue
        m = COUNT.match(line)
        if m:
            count = int(m.group(1))
    if not seen_header:
        raise RuntimeError("threads header not observed")
    if count is None:
        raise RuntimeError("threads count line not observed")
    if count != len(rows):
        raise RuntimeError(f"threads count mismatch: line says {count}, rows {len(rows)}")
    return rows, count


try:
    if not read_until(b"RIXURI: SHELL READY", 60.0):
        raise RuntimeError("embedded init completion not observed")
    if not read_until(b"\x1b[1;37m:\x1b[0m ", 60.0):
        raise RuntimeError("initial shell prompt not observed")
    # Settle: two consecutive snapshots must agree (no boot worker still
    # exiting). Up to 3 attempts; a moving baseline fails the run.
    base = None
    for _ in range(3):
        rows_a, count_a = snapshot_threads()
        rows_b, count_b = snapshot_threads()
        if count_a == count_b:
            base = (rows_a, count_a)
            break
        drain_quiet(idle=3.0, cap=20.0)
    if base is None:
        raise RuntimeError("thread baseline never settled")
    rows_a, count_a = base
    # Boot thread invariant: TID 1 owned by pid 0.
    if not any(t == 1 and o == 0 for (t, o, _s) in rows_a):
        raise RuntimeError("reserved boot thread TID 1 / OWNER 0 missing")
    if any(s == "D" for (_t, _o, s) in rows_a):
        raise RuntimeError("DETACHED thread lingers in settled baseline")
    max_before = max(t for (t, _o, _s) in rows_a)
    # Load: spawn and reap real processes (each ps creates a task+thread,
    # runs, exits, and is reaped by the shell).
    command(b"/usr/bin/ps")
    command(b"/usr/bin/ps")
    rows_c, count_c = snapshot_threads()
    if count_c != count_a:
        raise RuntimeError(f"thread leak: baseline {count_a}, after load {count_c}")
    max_after = max(t for (t, _o, _s) in rows_c)
    if max_after <= max_before:
        raise RuntimeError(f"TIDs not monotonic: before {max_before}, after {max_after}")
    if any(s == "D" for (_t, _o, s) in rows_c):
        raise RuntimeError("DETACHED thread lingers after load")
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
for marker in (b"CPU exception", b"PANIC"):
    if marker in output:
        raise SystemExit("kernel fault marker observed")
print("qemu thread lifecycle test: PASS")
