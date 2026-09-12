# Test Machine: ASUS PRIME B650M-R (owner workstation)

Reference hardware for RixuriOS physical bring-up. Facts below come
from owner messages, on-machine WMI queries and RixuriOS boot logs.
Nothing here is guessed: unknown items say TBD.

## Platform

- Board: ASUS PRIME B650M-R Rev 1.xx (micro-ATX, AM5, B650 chipset)
- CPU: AMD Ryzen 7 7700, 8C/16T (Zen 4 Raphael; >8 CPUs, so the kernel
  boots BSP-only by design until AP startup is serialized)
- RAM: 2x Kingston KF560C36-16 (Fury Beast DDR5-6000 CL36 kits),
  currently running at 4800 MT/s (EXPO off or board-limited)
- GPU: AMD Radeon RX 6800 XT discrete + Raphael integrated graphics
- BIOS: AMI 3035, 2024-05-09. Settings for RixuriOS boot: Secure Boot
  Off (`Other OS`), CSM Disabled, Fast Boot Disabled, boot via the
  `UEFI:` USB entry.
- OS: Windows 11 Pro 24H2 (build 26100, 64-bit)
- Displays: primary 1920x1080 landscape + secondary 1080x1920
  portrait (Alienware panels)

## On-board devices seen by RixuriOS

- Net: Realtek RTL8125 2.5GbE — TX path alive on hardware
  (`RTL8125: tx slot=...` in boot log).
- Storage: XPG GAMMIX S70 BLADE 1 TB NVMe (`nvme0n1`, 2000042964 x
  512B sectors = 1024209543168 bytes, matches Windows size). No RixFS
  superblock on it, so the kernel falls back to embedded init
  (expected; by design, not an error). A SanDisk Cruzer Blade 32 GB
  stick was present (the RixuriOS boot USB itself).
- USB: 4x AMD xHCI controllers (PCI IDs from Windows, 2026-09-12):
  - `1022:15B7` USB 3.10 xHCI 1.20 (Raphael) — likely ctl0, 18 ports
  - `1022:15B6` USB 3.10 xHCI 1.20 (Raphael) — 4 ports
  - `1022:43F7` USB 3.10 xHCI 1.10, ASMedia subsys (chipset) — 4 ports
  - `1022:15B8` USB 2.0 xHCI 1.20 (Raphael) — likely ctl3, 1 port
  (ctl numbering is the kernel's PCI scan order; exact ctl↔ID mapping
  TBD — confirm via the `AMD/PCI candidate` boot lines.)
- Observed failing ports (rc=4/7, PORTSC 0x331/0x6e1/0xae1/0xee1):
  ctl0 p9/p10/p12/p13/p15/p16, ctl1 p1, ctl2 p1, ctl3 p1.
  Speeds read 1/2/3 (USB2 family); no SuperSpeed device attached yet.

## Input situation

- Keyboard: USB (no PS/2 port or PS/2 keyboard available).
- Status: not working yet — H1/H2 port-reset recovery in progress.
  Shell reaches READY on the GOP console; no input path until a USB
  port attaches + enumerates + HID polls.

## TBD (waiting on owner)

- Which physical rear-panel ports map to which controller/port
  numbers (needed to aim the keyboard at a known-good USB2 port).
- BIOS settings dump if anything USB-related was changed from
  defaults (XHCI handoff, legacy USB support).
