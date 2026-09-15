#!/usr/bin/env python3
"""Write build/provenance.json for the current working tree (no CI).

Records source revision, tree cleanliness, epoch mode, host/toolchain
versions, and sha256+sizes of release artifacts when present (absent
artifacts are recorded as absent, never fabricated).
"""
import datetime
import hashlib
import json
import os
import platform
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "build"


def run(*args: str) -> str:
    try:
        out = subprocess.run(
            list(args), cwd=ROOT, capture_output=True, text=True, timeout=20
        )
    except Exception:
        return ""
    return (out.stdout or "").strip() if out.returncode == 0 else ""


def tool_version(cmd: str, *args: str) -> str:
    if not shutil.which(cmd):
        return "absent"
    first = run(cmd, *args).splitlines()
    return first[0].strip() if first else "present"


def sha256_of(path: Path):
    if not path.is_file():
        return None
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1048576), b""):
            h.update(chunk)
    return {"sha256": h.hexdigest(), "bytes": path.stat().st_size}


def main() -> int:
    epoch = os.environ.get("SOURCE_DATE_EPOCH", "")
    rev = run("git", "rev-parse", "HEAD") or "unknown"
    short_rev = run("git", "rev-parse", "--short", "HEAD") or rev[:12]
    dirty_rc = subprocess.run(
        ["git", "diff", "--quiet"], cwd=ROOT, timeout=30
    ).returncode
    generated = (
        datetime.datetime.fromtimestamp(
            int(epoch), tz=datetime.timezone.utc
        ).isoformat()
        if epoch.isdigit()
        else datetime.datetime.now(tz=datetime.timezone.utc).isoformat()
    )
    manifest = {
        "schema_version": 1,
        "revision": rev,
        "short_revision": short_rev,
        "dirty": dirty_rc != 0,
        "source_date_epoch": epoch if epoch.isdigit() else None,
        "generated_at": generated,
        "host": {
            "system": platform.system(),
            "release": platform.release(),
            "machine": platform.machine(),
        },
        "toolchain": {
            "host_gcc": tool_version("gcc", "--version"),
            "cross_gcc": tool_version("x86_64-linux-gnu-gcc", "--version"),
            "host_ld": tool_version("ld", "--version"),
            "cross_ld": tool_version("x86_64-linux-gnu-ld", "--version"),
            "uefi_gcc": tool_version("x86_64-w64-mingw32-gcc", "--version"),
            "qemu": tool_version("qemu-system-x86_64", "--version"),
            "mtools": tool_version("mcopy", "--version"),
            "mkfs_fat": tool_version("mkfs.fat", "--help"),
            "xorriso": tool_version("xorriso", "--version"),
            "python3": tool_version("python3", "--version"),
            "make": tool_version("make", "--version"),
        },
        "artifacts": {
            "kernel_elf": sha256_of(BUILD / "kernel.elf"),
            "esp_img": sha256_of(BUILD / "uefi" / "esp.img"),
            "iso": sha256_of(BUILD / "RixuriOS.iso"),
            "rixfs_img": sha256_of(BUILD / "rixfs.img"),
        },
    }
    BUILD.mkdir(parents=True, exist_ok=True)
    out = BUILD / "provenance.json"
    out.write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"provenance: {out}")
    print(f"revision={short_rev} dirty={manifest['dirty']}")
    for name, art in manifest["artifacts"].items():
        print(f"{name}: {art['sha256'][:16] + '...' if art else 'absent'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
