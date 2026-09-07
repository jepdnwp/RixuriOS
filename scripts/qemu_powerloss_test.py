#!/usr/bin/env python3
import os
import select
import shutil
import signal
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BASE = ROOT / "build" / "rixfs.img"
ESP = ROOT / "build" / "uefi" / "esp"
PROMPT = b"\x1b[1;37m:\x1b[0m "
FAULTS = (0.001, 0.010, 0.050, 0.150)
SCENARIOS = (
    ("add", b"/usr/bin/accountctl add auditor 1100 auditor-pass\n", b"/usr/bin/accountctl add auditor 1100 auditor-pass"),
    ("rotate", b"/usr/bin/accountctl rotate operator crash-rotated\n", b"/usr/bin/accountctl rotate operator recovery-pass"),
    ("remove", b"/usr/bin/accountctl remove auditor\n", b"/usr/bin/accountctl add auditor 1100 auditor-pass"),
)

if not BASE.exists() or not ESP.exists():
    raise SystemExit("missing build image/ESP; run make image first")

def boot(image: Path):
    env = os.environ.copy()
    env["RIXURI_RIXFS_IMAGE"] = str(image)
    env["RIXURI_ESP"] = str(ESP)
    return subprocess.Popen(["bash", "./scripts/run-qemu.sh"], cwd=ROOT,
                            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, env=env)

def wait_for(proc, output, marker, timeout):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if marker in output[0]:
            return True
        ready, _, _ = select.select([proc.stdout], [], [], 0.1)
        if ready:
            chunk = os.read(proc.stdout.fileno(), 4096)
            if not chunk:
                return False
            output[0].extend(chunk)
    return False

def stop(proc, kill=False):
    if proc.poll() is not None:
        return
    if kill:
        os.kill(proc.pid, signal.SIGKILL)
    else:
        proc.terminate()
    try:
        proc.wait(timeout=3)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()

def send(proc, output, data):
    before = len(output[0])
    proc.stdin.write(data)
    proc.stdin.flush()
    end = time.monotonic() + 25.0
    while time.monotonic() < end:
        if PROMPT in output[0][before:]:
            return
        ready, _, _ = select.select([proc.stdout], [], [], 0.1)
        if ready:
            chunk = os.read(proc.stdout.fileno(), 4096)
            if not chunk:
                break
            output[0].extend(chunk)
    raise RuntimeError("prompt timeout after command")

results = []
for scenario, command, repair in SCENARIOS:
    for delay in FAULTS:
        image = ROOT / "build" / f"powerloss-{scenario}-{int(delay * 1000):03d}ms.img"
        shutil.copyfile(BASE, image)
        crash = boot(image)
        try:
            output = [bytearray()]
            if not wait_for(crash, output, b"RIXURI:KERNEL_READY", 30.0):
                raise RuntimeError("boot failed before injection")
            if not wait_for(crash, output, PROMPT, 20.0):
                raise RuntimeError("prompt failed before injection")
            crash.stdin.write(command)
            crash.stdin.flush()
            time.sleep(delay)
        finally:
            stop(crash, kill=True)

        recovery = boot(image)
        try:
            recovered = [bytearray()]
            if not wait_for(recovery, recovered, b"RIXURI:KERNEL_READY", 30.0):
                raise RuntimeError("recovery boot failed")
            if not wait_for(recovery, recovered, PROMPT, 20.0):
                raise RuntimeError("recovery prompt failed")
            send(recovery, recovered, repair + b"\n")
            if scenario == "rotate":
                send(recovery, recovered, b"/usr/bin/authcheck verify operator recovery-pass\n")
            else:
                send(recovery, recovered, b"/usr/bin/authcheck verify operator phase20-pass\n")
            combined = bytes(recovered[0])
            if any(marker in combined for marker in (b"PAGE FAULT", b"CPU exception", b"PANIC")):
                raise RuntimeError("fault marker after recovery")
        finally:
            stop(recovery)
            image.unlink(missing_ok=True)
        results.append(f"{scenario:7s} kill={delay * 1000:6.1f}ms recovery=PASS")

print("qemu power-loss recovery: PASS")
for result in results:
    print(result)
