import os
import select
import shutil
import subprocess
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
out = bytearray()
cur = 0

with tempfile.TemporaryDirectory(prefix="rixurios-session-") as temporary:
    temporary_root = Path(temporary)
    test_image = temporary_root / "rixfs.img"
    test_esp = temporary_root / "esp"
    shutil.copy2(ROOT / "build/rixfs.img", test_image)
    shutil.copytree(ROOT / "build/uefi/esp", test_esp)
    environment = os.environ.copy()
    environment["RIXURI_RIXFS_IMAGE"] = str(test_image)
    environment["RIXURI_ESP"] = str(test_esp)
    process = subprocess.Popen(
        ["bash", "./scripts/run-qemu.sh"],
        cwd=ROOT,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        env=environment,
    )

    def until(marker, timeout=30):
        global cur
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            index = out.find(marker, cur)
            if index >= 0:
                cur = index + len(marker)
                return True
            ready, _, _ = select.select([process.stdout], [], [], 0.2)
            if ready:
                chunk = os.read(process.stdout.fileno(), 4096)
                if not chunk:
                    return False
                out.extend(chunk)
        return False

    try:
        if not until(b"USER: init returned to kernel"):
            raise RuntimeError("boot")
        time.sleep(1)
        process.stdin.write(b"/usr/bin/sessiontest\n")
        process.stdin.flush()
        if not until(b"rixuri$ ", 20):
            raise RuntimeError("prompt")
    finally:
        process.terminate()
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
        Path("/tmp/session-qemu.log").write_bytes(out)

Path("/tmp/session-qemu.log").write_bytes(out)
print(out.decode("utf-8", "replace"))
if b"session=PASS" not in out:
    raise SystemExit("missing session pass marker")
if b"PAGE FAULT" in out or b"CPU exception" in out or b"PANIC" in out:
    raise SystemExit("fault")
print("qemu session lifecycle test: PASS")
