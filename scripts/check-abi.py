#!/usr/bin/env python3
"""Verify docs/ABI_REGISTRY.md matches kernel/syscall/syscall.h.

The header is normative; the registry is descriptive. Any drift between
them is a bug: every RIX_SYS_* number in the header must appear in the
registry with the same short name, and vice versa. Duplicate numbers or
names in either file also fail.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HEADER = ROOT / "kernel" / "syscall" / "syscall.h"
REGISTRY = ROOT / "docs" / "ABI_REGISTRY.md"

header_re = re.compile(r"#define\s+(RIX_SYS_[A-Z0-9_]+)\s+(\d+)")
row_re = re.compile(r"^\|\s*(\d+)\s*\|\s*([A-Z0-9_ ]+?)\s*\|")


def main() -> int:
    header_text = HEADER.read_text()
    header_nums: dict[int, str] = {}
    header_names: dict[str, int] = {}
    errors: list[str] = []
    for m in header_re.finditer(header_text):
        name, num = m.group(1), int(m.group(2))
        short = name[len("RIX_SYS_") :]
        if num in header_nums:
            errors.append(f"header: duplicate number {num} ({header_nums[num]} vs {short})")
        if short in header_names:
            errors.append(f"header: duplicate name {short}")
        header_nums[num] = short
        header_names[short] = num

    reg_nums: dict[int, str] = {}
    for line in REGISTRY.read_text().splitlines():
        m = row_re.match(line.strip())
        if not m:
            continue
        num, raw = int(m.group(1)), m.group(2).strip()
        # Rows like "39/140" are handled per-number below; skip composites here.
        name = raw.split()[0]
        if num in reg_nums:
            errors.append(f"registry: duplicate number {num}")
        reg_nums[num] = name

    for num, short in sorted(header_nums.items()):
        if num not in reg_nums:
            errors.append(f"missing in registry: {num} {short}")
        elif reg_nums[num] != short:
            # Composite rows (e.g. 39/140) list one name; accept either side.
            errors.append(
                f"name drift at {num}: header {short} vs registry {reg_nums[num]}"
            )
    for num, name in sorted(reg_nums.items()):
        if num not in header_nums:
            errors.append(f"missing in header: {num} {name}")

    if errors:
        print("ABI drift detected:")
        for e in errors:
            print(f"  - {e}")
        return 1
    print(f"ABI registry consistent: {len(header_nums)} syscalls")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
