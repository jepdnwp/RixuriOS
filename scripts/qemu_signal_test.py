#!/usr/bin/env python3
import os
import select
import subprocess
import sys
import time

proc = subprocess.Popen(
    ["bash", "./scripts/run-qemu.sh"],
    stdin=subprocess.PIPE,
    stdout=subprocess.PIPE,
    stderr=subprocess.STDOUT,
)
output = bytearray()

def read_until(text, timeout):
    deadline = time.monotonic() + timeout
    marker = text.encode()
    while time.monotonic() < deadline:
        ready, _, _ = select.select([proc.stdout], [], [], 0.2)
        if ready:
            chunk = os.read(proc.stdout.fileno(), 4096)
            if not chunk:
                return False
            output.extend(chunk)
            if marker in output:
                return True
    return False

try:
    if not read_until("rixuri$ ", 12):
        raise RuntimeError("shell prompt not observed")
    proc.stdin.write(b"/bin/cat\n")
    proc.stdin.flush()
    time.sleep(2)
    proc.stdin.write(b"\x03")
    proc.stdin.flush()
    time.sleep(3)
finally:
    proc.terminate()
    try:
        proc.wait(timeout=3)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()

text = output.decode("utf-8", "replace")
sys.stdout.write(text)
if "rixuri: command execution failed" not in text:
    raise SystemExit("foreground Ctrl-C interruption was not observed")
if "exception" in text.lower() or "page fault" in text.lower():
    raise SystemExit("kernel fault observed during Ctrl-C test")
print("qemu signal test: PASS")
