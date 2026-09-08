#!/usr/bin/env python3
import struct
import sys
from pathlib import Path
path = Path(sys.argv[1])
data = path.read_bytes()
print(f"pcap_bytes={len(data)}")
if len(data) < 24:
    raise SystemExit("short pcap")
magic, major, minor, zone, sigfigs, snaplen, network = struct.unpack_from("<IHHIIII", data, 0)
print(f"magic=0x{magic:08x} version={major}.{minor} snaplen={snaplen} network={network}")
offset = 24
index = 0
while offset + 16 <= len(data):
    sec, usec, included, original = struct.unpack_from("<IIII", data, offset)
    offset += 16
    frame = data[offset:offset + included]
    offset += included
    index += 1
    if len(frame) >= 14:
        dst = ":".join(f"{b:02x}" for b in frame[0:6])
        src = ":".join(f"{b:02x}" for b in frame[6:12])
        ethertype = int.from_bytes(frame[12:14], "big")
        print(f"frame={index} len={len(frame)} dst={dst} src={src} ethertype=0x{ethertype:04x}")
        if ethertype == 0x0806 and len(frame) >= 42:
            operation = int.from_bytes(frame[20:22], "big")
            sender_ip = ".".join(str(b) for b in frame[28:32])
            target_ip = ".".join(str(b) for b in frame[38:42])
            print(f"  arp_op={operation} sender_ip={sender_ip} target_ip={target_ip}")
        if ethertype == 0x0800 and len(frame) >= 34:
            protocol = frame[23]
            source = ".".join(str(b) for b in frame[26:30])
            target = ".".join(str(b) for b in frame[30:34])
            print(f"  ipv4_protocol={protocol} source={source} target={target}")
print(f"frames={index}")
