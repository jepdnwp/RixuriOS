#!/usr/bin/env python3
"""Normalize FAT32 directory-entry timestamps for reproducible ESP images.

Sets every short directory entry's create/access/write date+time fields to
the DOS encoding of SOURCE_DATE_EPOCH (UTC). Long-name entries (attr 0x0F)
carry no timestamps and are left alone. Volume ID is set separately via
`mkfs.fat -i` in build-uefi.sh; this script only touches timestamps.
"""
import argparse
import datetime
import struct
import sys


def dos_datetime(epoch: int):
    dt = datetime.datetime.fromtimestamp(epoch, tz=datetime.timezone.utc)
    year = max(1980, dt.year)
    dos_date = ((year - 1980) << 9) | (dt.month << 5) | dt.day
    dos_time = (dt.hour << 11) | (dt.minute << 5) | (dt.second // 2)
    return dos_date, dos_time


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--image", required=True)
    ap.add_argument("--epoch", required=True, type=int)
    args = ap.parse_args()

    dos_date, dos_time = dos_datetime(args.epoch)
    with open(args.image, "r+b") as f:
        boot = f.read(512)
        if len(boot) < 512 or boot[510] != 0x55 or boot[511] != 0xAA:
            print("not a bootable FAT image", file=sys.stderr)
            return 1
        bps = struct.unpack_from("<H", boot, 11)[0]
        spc = boot[13]
        reserved = struct.unpack_from("<H", boot, 14)[0]
        fats = boot[16]
        fat_sz = struct.unpack_from("<I", boot, 36)[0]
        root_cluster = struct.unpack_from("<I", boot, 44)[0]
        if bps not in (512, 1024, 2048, 4096) or spc == 0 or fats == 0:
            print("unsupported FAT geometry", file=sys.stderr)
            return 1
        data_start = reserved + fats * fat_sz
        fat_start = reserved

        def cluster_offset(cluster: int) -> int:
            return (data_start + (cluster - 2) * spc) * bps

        def fat_entry(cluster: int) -> int:
            f.seek((fat_start * bps) + cluster * 4)
            raw = f.read(4)
            return struct.unpack("<I", raw)[0] & 0x0FFFFFFF

        def walk_cluster(cluster: int, visited: set):
            while cluster not in visited:
                if cluster < 2 or cluster >= 0x0FFFFFF8:
                    return
                visited.add(cluster)
                base = cluster_offset(cluster)
                for idx in range((spc * bps) // 32):
                    off = base + idx * 32
                    f.seek(off)
                    ent = bytearray(f.read(32))
                    if len(ent) < 32:
                        return
                    first = ent[0]
                    if first == 0x00:
                        return
                    attr = ent[11]
                    if attr == 0x0F:
                        continue
                    # Normalize timestamps on live and deleted short entries.
                    struct.pack_into("<H", ent, 14, dos_time)
                    struct.pack_into("<H", ent, 16, dos_date)
                    struct.pack_into("<H", ent, 18, dos_date)
                    struct.pack_into("<H", ent, 22, dos_time)
                    struct.pack_into("<H", ent, 24, dos_date)
                    f.seek(off)
                    f.write(ent)
                    if first == 0xE5:
                        continue
                    if attr & 0x10:
                        name = bytes(ent[0:8]).decode("ascii", "replace")
                        if name.startswith((".", "..      ", "..")):
                            continue
                        if name[0:1] == ".":
                            continue
                        lo = struct.unpack_from("<H", ent, 26)[0]
                        hi = struct.unpack_from("<H", ent, 20)[0]
                        sub = (hi << 16) | lo
                        if sub >= 2:
                            walk_cluster(sub, visited)
                cluster = fat_entry(cluster)

        walk_cluster(root_cluster if root_cluster >= 2 else 2, set())
    print(f"normalized FAT timestamps to epoch {args.epoch}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
