# Test Machine: ASUS PRIME B650M-R (owner workstation)

Reference hardware for RixuriOS physical bring-up. Facts below come
from owner messages and RixuriOS boot logs unless marked TBD
(dxdiag pending). Nothing here is guessed: unknown items say TBD.

## Platform

- Board: ASUS PRIME B650M-R (micro-ATX, AM5, B650 chipset)
- CPU: AMD Ryzen 7 7700, 8C/16T (Zen 4 Raphael; >8 CPUs, so the kernel
  boots BSP-only by design until AP startup is serialized)
- RAM: 32 GB Kingston DDR5-4800
- GPU: AMD Radeon RX 6800 XT discrete + Raphael integrated graphics
- BIOS: AMI 3035, 2024-05-09. Settings for RixuriOS boot: Secure Boot
  Off (`Other OS`), CSM Disabled, Fast Boot Disabled, boot via the
  `UEFI:` USB entry.
- OS: Windows 11 Pro (dxdiag pending for exact build/driver versions)
- Display: Alienware monitor (panel resolution TBD)

## On-board devices seen by RixuriOS

- Net: Realtek RTL8125 2.5GbE — TX path alive on hardware
  (`RTL8125: tx slot=...` in boot log).
- Storage: NVMe 1 TB (`nvme0n1`, 2000042964 x 512B sectors). No RixFS
  superblock on it, so the kernel falls back to embedded init
  (expected; by design, not an error).
- USB: 4x xHCI controllers (TBD: PCI vendor:device IDs per
  controller — dxdiag or `xHCI: AMD/PCI candidate` boot lines):
  - ctl0: 18 ports, 127 slots (chipset root complex, TBD exact part)
  - ctl1: 4 ports (TBD)
  - ctl2: 4 ports (TBD)
  - ctl3: 1 port (TBD)
- Observed failing ports (rc=4/7, PORTSC 0x331/0x6e1/0xae1/0xee1):
  ctl0 p9/p10/p12/p13/p15/p16, ctl1 p1, ctl2 p1, ctl3 p1.
  Speeds read 1/2/3 (USB2 family); no SuperSpeed device attached yet.

## Input situation

- Keyboard: USB (no PS/2 port or PS/2 keyboard available).
- Status: not working yet — H1/H2 port-reset recovery in progress.
  Shell reaches READY on the GOP console; no input path until a USB
  port attaches + enumerates + HID polls.

## TBD (waiting on dxdiag / owner)

- Exact PCI IDs for the 4 xHCI controllers (vendor:device per ctl).
- Which physical rear-panel ports map to which controller/port
  numbers (needed to aim the keyboard at a known-good USB2 port).
- NVMe exact model, RAM exact part number, Windows build number.
- BIOS settings dump if anything USB-related was changed from
  defaults (XHCI handoff, legacy USB support).
