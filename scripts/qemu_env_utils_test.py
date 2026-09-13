#!/usr/bin/env python3
import os
import select
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
IMAGE = ROOT / "build" / "rixfs-env-utils.img"
ESP = ROOT / "build" / "uefi" / "esp-env-utils"
LOG = ROOT / "build" / "qemu-env-utils.log"
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


def drain_output(duration: float = 0.25) -> None:
    """Collect UART bytes that can arrive just after the shell prompt.

    The kernel serial path and QEMU pipe are asynchronous: a child error
    write may become readable a fraction after the parent redraws its prompt.
    Draining here prevents the next command from consuming that evidence.
    """
    deadline = time.monotonic() + duration
    while time.monotonic() < deadline:
        ready, _, _ = select.select([proc.stdout], [], [], 0.02)
        if not ready:
            continue
        chunk = os.read(proc.stdout.fileno(), 4096)
        if not chunk:
            return
        output.extend(chunk)


def drain_quiet(idle: float = 2.0, cap: float = 15.0) -> None:
    """Wait until the guest goes quiet before snapshotting.

    Prompt matching can succeed while a slow child (loaded host, serial
    backlog) still has output in flight; killing QEMU at that point
    loses the evidence and fails assertions on the last command. Waiting
    for a full idle window keeps every existing assertion intact while
    making the observation complete.
    """
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


def command(line: bytes) -> None:
    for byte in line + b"\n":
        proc.stdin.write(bytes((byte,)))
        proc.stdin.flush()
        time.sleep(0.01)
    if not read_until(b"\x1b[1;37m:\x1b[0m ", 25.0):
        raise RuntimeError(f"prompt not observed after {line!r}")
    drain_output()


try:
    if not read_until(b"RIXURI: SHELL READY", 30.0):
        raise RuntimeError("embedded init completion not observed")
    time.sleep(1.0)
    command(b"/usr/bin/env")
    command(b"/usr/bin/printf x=%s,n=%d hello 42")
    command(b"/bin/pwd")
    command(b"/usr/bin/which echo")
    command(b"/usr/bin/which absent-command")
    drain_quiet()
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
if b"PATH=/bin:/usr/bin:/sbin:/usr/sbin" not in output:
    raise SystemExit("env output not observed")
if b"x=hello,n=42" not in output:
    raise SystemExit("printf output not observed")
if b"/home/root\n" not in output:
    raise SystemExit("pwd output not observed")
if b"/bin/echo" not in output:
    raise SystemExit("which output not observed")
if b"which: not found" not in output:
    raise SystemExit("which missing-command failure not observed")
if b"CPU exception" in output or b"PANIC" in output:
    raise SystemExit("kernel fault marker observed")
print("qemu environment utilities test: PASS")
