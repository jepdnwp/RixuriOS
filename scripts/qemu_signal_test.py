#!/usr/bin/env python3
import os
import select
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def read_until(proc, output, cursor, marker, timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        index = output.find(marker, cursor)
        if index >= 0:
            return index + len(marker)
        ready, _, _ = select.select([proc.stdout], [], [], 0.2)
        if ready:
            chunk = os.read(proc.stdout.fileno(), 4096)
            if not chunk:
                return None
            output.extend(chunk)
    return None


def send_bytes(proc, data):
    for byte in data:
        proc.stdin.write(bytes((byte,)))
        proc.stdin.flush()
        time.sleep(0.01)


def run_scenario(name, control):
    image = ROOT / "build" / f"rixfs-signal-{name}.img"
    esp = ROOT / "build" / "uefi" / f"esp-signal-{name}"
    shutil.copyfile(ROOT / "build" / "rixfs.img", image)
    shutil.copytree(ROOT / "build" / "uefi" / "esp", esp)
    env = os.environ.copy()
    env["RIXURI_RIXFS_IMAGE"] = str(image)
    env["RIXURI_ESP"] = str(esp)
    proc = subprocess.Popen(
        ["bash", "./scripts/run-qemu.sh"],
        cwd=ROOT,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        env=env,
    )
    output = bytearray()
    cursor = 0
    try:
        cursor = read_until(proc, output, cursor, b"USER: init returned to kernel", 30.0)
        if cursor is None:
            raise RuntimeError(f"{name}: embedded init completion not observed")
        time.sleep(1.0)
        send_bytes(proc, b"/bin/cat\n")
        time.sleep(2.0)
        send_bytes(proc, bytes((control,)))
        cursor = read_until(proc, output, cursor, b"\x1b[1;37m:\x1b[0m ", 10.0)
        if cursor is None:
            raise RuntimeError(f"{name}: shell prompt did not return")
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()
        log_path = ROOT / "build" / f"qemu-signal-{name}.log"
        log_path.write_bytes(output)
        image.unlink(missing_ok=True)
        shutil.rmtree(esp, ignore_errors=True)
    text = output.decode("utf-8", "replace")
    lowered = text.lower()
    if any(marker in lowered for marker in ("exception", "page fault", "panic")):
        raise RuntimeError(f"{name}: kernel fault marker observed")
    print(f"{name}: shell prompt returned after control signal; "
          f"command_failure_message={'yes' if 'rixuri: command execution failed' in text else 'no'}")
    return output


scenarios = [("ctrl-c", 0x03), ("ctrl-z", 0x1A), ("ctrl-backslash", 0x1C)]
if len(sys.argv) > 1:
    scenarios = [scenario for scenario in scenarios if scenario[0] == sys.argv[1]]
    if not scenarios:
        raise SystemExit(f"unknown signal scenario: {sys.argv[1]}")
for name, control in scenarios:
    run_scenario(name, control)
print("qemu foreground signal tests: PASS")
