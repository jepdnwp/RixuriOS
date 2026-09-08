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
t0 = None
while offset + 16 <= len(data):
    sec, usec, included, original = struct.unpack_from("<IIII", data, offset)
    offset += 16
    frame = data[offset:offset + included]
    offset += included
    index += 1
    if t0 is None:
        t0 = sec + usec / 1000000.0
    stamp = sec + usec / 1000000.0 - t0
    if len(frame) >= 14:
        dst = ":".join(f"{b:02x}" for b in frame[0:6])
        src = ":".join(f"{b:02x}" for b in frame[6:12])
        ethertype = int.from_bytes(frame[12:14], "big")
        print(f"frame={index} t={stamp:.3f}s len={len(frame)} dst={dst} src={src} ethertype=0x{ethertype:04x}")
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
            ver_ihl = frame[14]
            ip_total = int.from_bytes(frame[16:18], "big")
            print(f"  ipv4 ver={ver_ihl >> 4} ihl={ver_ihl & 0x0f} ip_total={ip_total} tcp_dataoff={frame[46] >> 4}")
            if protocol == 6 and len(frame) >= 54:
                sport = int.from_bytes(frame[34:36], "big")
                dport = int.from_bytes(frame[36:38], "big")
                seq = int.from_bytes(frame[38:42], "big")
                ack = int.from_bytes(frame[42:46], "big")
                flags = frame[47]
                flag_names = "".join(n for b, n in ((0x01, "F"), (0x02, "S"), (0x04, "R"),
                                                     (0x08, "P"), (0x10, "A")) if flags & b)
                print(f"  tcp {sport}>{dport} seq={seq} ack={ack} flags={flag_names or hex(flags)}")
                tcp_off = 14 + 20
                tcp_len_wire = len(frame) - tcp_off
                tcp_len_ip = max(0, ip_total - 20)
                pad = frame[tcp_off + tcp_len_ip:] if tcp_len_ip < tcp_len_wire else b""
                print(f"  tcp ip_total={ip_total} wire_len={tcp_len_wire} pad={pad.hex() or 'none'}")

                def csum16(words):
                    s = 0
                    for i in range(0, len(words) - 1, 2):
                        s += (words[i] << 8) | words[i + 1]
                    if len(words) % 2:
                        s += words[-1] << 8
                    while s >> 16:
                        s = (s & 0xffff) + (s >> 16)
                    return (~s) & 0xffff

                def pseudo(src, dst, seg):
                    ph = (src[0] << 24 | src[1] << 16 | src[2] << 8 | src[3])
                    ph2 = (dst[0] << 24 | dst[1] << 16 | dst[2] << 8 | dst[3])
                    s = (ph >> 16) + (ph & 0xffff) + (ph2 >> 16) + (ph2 & 0xffff)
                    s += 6 + len(seg)
                    data = bytes(seg)
                    for i in range(0, len(data) - 1, 2):
                        s += (data[i] << 8) | data[i + 1]
                    if len(data) % 2:
                        s += data[-1] << 8
                    while s >> 16:
                        s = (s & 0xffff) + (s >> 16)
                    return (~s) & 0xffff

                src_ip = frame[26:30]
                dst_ip = frame[30:34]
                seg_full = frame[tcp_off:]
                seg_decl = frame[tcp_off:tcp_off + tcp_len_ip]
                print(f"  tcp csum_full={pseudo(src_ip, dst_ip, seg_full):#06x} "
                      f"csum_declared_len={pseudo(src_ip, dst_ip, seg_decl):#06x}")
print(f"frames={index}")
