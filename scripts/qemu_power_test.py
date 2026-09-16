#!/usr/bin/env python3
"""Reboot/poweroff user ABI proof. The guest runs with -no-reboot and
-no-shutdown, so a successful machine reset/poweroff exits QEMU instead of
cycling. Each scenario boots a private image, runs one command, and requires
the QEMU process to exit promptly with no fault markers beforehand."""
import os
import select
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def run_scenario(tag: str, command: bytes) -> bytes:
    image = ROOT / "build" / f"rixfs-{tag}.img"
    esp = ROOT / "build" / "uefi" / f"esp-{tag}"
    log = ROOT / "build" / f"qemu-{tag}.log"
    shutil.copyfile(ROOT / "build" / "rixfs.img", image)
    shutil.rmtree(esp, ignore_errors=True)
    shutil.copytree(ROOT / "build" / "uefi" / "esp", esp)
    env = os.environ.copy()
    env["RIXURI_RIXFS_IMAGE"] = str(image)
    env["RIXURI_ESP"] = str(esp)
    proc = subprocess.Popen(
        ["bash", "./scripts/run-qemu.sh"], cwd=ROOT, stdin=subprocess.PIPE,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env,
    )
    output = bytearray()
    cursor = 0

    def read_until(marker: bytes, timeout: float) -> bool:
        nonlocal cursor
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

    try:
        if not read_until(b"RIXURI: SHELL READY", 30.0):
            raise RuntimeError(f"{tag}: embedded init completion not observed")
        time.sleep(1.0)
        for byte in command + b"\n":
            proc.stdin.write(bytes((byte,)))
            proc.stdin.flush()
            time.sleep(0.01)
        try:
            proc.wait(timeout=30.0)
        except subprocess.TimeoutExpired:
            raise RuntimeError(f"{tag}: QEMU did not exit after command")
    finally:
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
        # Drain any final bytes (fault markers would appear here).
        try:
            while True:
                ready, _, _ = select.select([proc.stdout], [], [], 0.2)
                if not ready:
                    break
                chunk = os.read(proc.stdout.fileno(), 4096)
                if not chunk:
                    break
                output.extend(chunk)
        except OSError:
            pass
        image.unlink(missing_ok=True)
        shutil.rmtree(esp, ignore_errors=True)
        log.write_bytes(output)
        sys.stdout.buffer.write(output)
    return bytes(output)


reboot_out = run_scenario("power-reboot", b"/sbin/reboot")
if b"CPU exception" in reboot_out or b"PANIC" in reboot_out:
    raise SystemExit("kernel fault marker observed before reboot")
poweroff_out = run_scenario("power-poweroff", b"/sbin/poweroff")
if b"CPU exception" in poweroff_out or b"PANIC" in poweroff_out:
    raise SystemExit("kernel fault marker observed before poweroff")
print("qemu power test: PASS")
