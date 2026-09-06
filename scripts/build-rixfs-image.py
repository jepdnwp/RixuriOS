#!/usr/bin/env python3
"""Build a disposable RixFS v1 image for QEMU integration tests.

The on-disk layout intentionally mirrors kernel/fs/rixfs_ops.c.  This tool is
not a second filesystem implementation: it only creates a deterministic test
image containing a small directory tree and immutable test files.
"""

from __future__ import annotations

import argparse
import math
import os
import struct
from pathlib import Path

SECTOR_SIZE = 512
IMAGE_SECTORS = 131072  # 64 MiB
INODE_SIZE = 128
INODE_COUNT = 256
DIRECT_EXTENTS = 4
MAGIC = 0x5249584653465331
VERSION = 1
IFREG = 0x8000
IFDIR = 0x4000
DIR_FILE = 2
DIR_DIR = 1
JOURNAL_SECTORS = 2


def fnv1a(data: bytes) -> int:
    # Keep this seed identical to kernel/fs/rixfs.c and rixfs_ops.c.
    value = 1469598103934665603
    for byte in data:
        value = ((value ^ byte) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return value


def ceil_div(value: int, divisor: int) -> int:
    return (value + divisor - 1) // divisor


def superblock(total_sectors: int, free_hint: int) -> bytes:
    inode_sectors = ceil_div(INODE_COUNT * INODE_SIZE, SECTOR_SIZE)
    bitmap_sectors = ceil_div(total_sectors, 8 * SECTOR_SIZE)
    bitmap_sector = 1 + inode_sectors
    journal_sector = bitmap_sector + bitmap_sectors
    data_start = journal_sector + JOURNAL_SECTORS
    fields = struct.pack(
        "<QIIII13Q",
        MAGIC,
        VERSION,
        SECTOR_SIZE,
        SECTOR_SIZE,
        0,
        total_sectors,
        1,
        INODE_COUNT,
        data_start,
        1,
        1,
        0,
        bitmap_sector,
        bitmap_sectors,
        journal_sector,
        JOURNAL_SECTORS,
        free_hint,
        0,
    )
    block = bytearray(fields + bytes(384))
    checksum_offset = 8 + 4 * 4 + 6 * 8
    struct.pack_into("<Q", block, checksum_offset, 0)
    struct.pack_into("<Q", block, checksum_offset, fnv1a(block))
    return bytes(block)


def inode(number: int, mode: int, size: int, extents: list[tuple[int, int]]) -> bytes:
    starts = [0] * DIRECT_EXTENTS
    lengths = [0] * DIRECT_EXTENTS
    for index, (start, length) in enumerate(extents[:DIRECT_EXTENTS]):
        starts[index] = start
        lengths[index] = length
    return struct.pack(
        "<QIIIIQQ4Q4Q6I",
        number,
        mode,
        0,
        0,
        0,
        size,
        1,
        *starts,
        *lengths,
        1,
        0xFFFFFFFF,
        0,
        0xFFFFFFFF,
        0,
        7,
    )


def dir_record(number: int, name: str) -> bytes:
    encoded = name.encode("utf-8")
    if not encoded or len(encoded) > 255 or "/" in name:
        raise ValueError(f"invalid directory entry name: {name!r}")
    record = bytearray(SECTOR_SIZE)
    struct.pack_into("<QHBBI", record, 0, number, SECTOR_SIZE, DIR_FILE, len(encoded), 0)
    record[16 : 16 + len(encoded)] = encoded
    return bytes(record)


def dir_record_dir(number: int, name: str) -> bytes:
    record = bytearray(dir_record(number, name))
    record[10] = DIR_DIR
    return bytes(record)


def normalize_path(value: str) -> str:
    if not value.startswith("/") or value == "/":
        raise ValueError(f"image paths must be absolute and non-root: {value!r}")
    parts = [part for part in value.split("/") if part]
    if not parts or any(part in (".", "..") for part in parts):
        raise ValueError(f"invalid image path: {value!r}")
    return "/" + "/".join(parts)


def parent_path(path: str) -> str:
    parent = path.rsplit("/", 1)[0]
    return parent or "/"


def build_image(output: Path, files: list[tuple[str, Path]], size_mib: int) -> None:
    total_sectors = size_mib * 1024 * 1024 // SECTOR_SIZE
    if total_sectors < 128:
        raise ValueError("image is too small")
    inode_sectors = ceil_div(INODE_COUNT * INODE_SIZE, SECTOR_SIZE)
    bitmap_sectors = ceil_div(total_sectors, 8 * SECTOR_SIZE)
    bitmap_sector = 1 + inode_sectors
    journal_sector = bitmap_sector + bitmap_sectors
    data_start = journal_sector + JOURNAL_SECTORS

    entries: dict[str, tuple[Path | None, bool]] = {"/": (None, True)}
    for image_path, host_path in files:
        image_path = normalize_path(image_path)
        if not host_path.is_file():
            raise FileNotFoundError(host_path)
        current = ""
        components = image_path.strip("/").split("/")
        for component in components[:-1]:
            current += "/" + component
            entries.setdefault(current, (None, True))
        entries[image_path] = (host_path, False)

    # Allocate inode numbers in deterministic depth/name order so an image
    # rebuild has stable metadata even if the argument order changes.
    paths = sorted(entries, key=lambda path: (path.count("/"), path))
    inode_numbers = {path: index + 1 for index, path in enumerate(paths)}
    children: dict[str, list[str]] = {path: [] for path in paths if entries[path][1]}
    for path in paths:
        if path == "/":
            continue
        children[parent_path(path)].append(path)
    for values in children.values():
        values.sort()

    required_dir_sectors = sum(len(values) for values in children.values())
    cursor = data_start
    allocations: dict[str, list[tuple[int, int]]] = {}
    for path in paths:
        if not entries[path][1]:
            continue
        child_count = len(children[path])
        if child_count == 0:
            allocations[path] = []
            continue
        allocations[path] = [(cursor, child_count)]
        cursor += child_count

    file_data: dict[str, bytes] = {}
    for path in paths:
        host_path, is_dir = entries[path]
        if is_dir:
            continue
        data = host_path.read_bytes()  # type: ignore[union-attr]
        file_data[path] = data
        sector_count = max(1, ceil_div(len(data), SECTOR_SIZE))
        allocations[path] = [(cursor, sector_count)]
        cursor += sector_count

    if cursor >= total_sectors:
        raise ValueError("RixFS image has no room for metadata and files")

    image = bytearray(total_sectors * SECTOR_SIZE)
    bitmap = bytearray(bitmap_sectors * SECTOR_SIZE)

    def mark(sector: int) -> None:
        bitmap[sector // 8] |= 1 << (sector % 8)

    for sector in range(data_start):
        mark(sector)
    for extents in allocations.values():
        for start, length in extents:
            for sector in range(start, start + length):
                mark(sector)

    image[:SECTOR_SIZE] = superblock(total_sectors, cursor)
    inode_base = SECTOR_SIZE
    for path in paths:
        number = inode_numbers[path]
        host_path, is_dir = entries[path]
        mode = (IFDIR if is_dir else IFREG) | (0o6755 if path.endswith("/id") else (0o755 if is_dir or path.endswith(("/echo", "/cat", "/grep", "/true", "/false", "/args")) else 0o644))
        size = len(children[path]) * SECTOR_SIZE if is_dir else len(file_data[path])
        payload = inode(number, mode, size, allocations[path])
        offset = inode_base + (number - 1) * INODE_SIZE
        image[offset : offset + INODE_SIZE] = payload

    bitmap_offset = bitmap_sector * SECTOR_SIZE
    image[bitmap_offset : bitmap_offset + len(bitmap)] = bitmap

    for path in paths:
        if not entries[path][1]:
            continue
        records = []
        for child in children[path]:
            child_name = child.rsplit("/", 1)[1]
            records.append(
                dir_record_dir(inode_numbers[child], child_name)
                if entries[child][1]
                else dir_record(inode_numbers[child], child_name)
            )
        for index, record in enumerate(records):
            sector = allocations[path][0][0] + index
            start = sector * SECTOR_SIZE
            image[start : start + SECTOR_SIZE] = record

    for path, data in file_data.items():
        start_sector = allocations[path][0][0]
        start = start_sector * SECTOR_SIZE
        image[start : start + len(data)] = data

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(image)

    print(f"RixFS image: {output}")
    print(f"size: {size_mib} MiB, sectors: {total_sectors}, data_start: {data_start}")
    print("files:")
    for path in sorted(file_data):
        print(f"  {path} ({len(file_data[path])} bytes)")
    print(f"free_hint: {cursor}, metadata/data sectors allocated: {cursor}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("-o", "--output", type=Path, required=True)
    parser.add_argument("--size-mib", type=int, default=64)
    parser.add_argument(
        "--file",
        action="append",
        default=[],
        metavar="/image/path=host/path",
        help="embed one file; may be repeated",
    )
    args = parser.parse_args()
    files: list[tuple[str, Path]] = []
    for specification in args.file:
        if "=" not in specification:
            parser.error(f"--file requires /image/path=host/path: {specification!r}")
        image_path, host_path = specification.split("=", 1)
        files.append((image_path, Path(host_path)))
    build_image(args.output, files, args.size_mib)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
