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
- USB: 4x AMD xHCI controllers. PCI IDs plus bus/device/function read
  on this board from the Windows 11 registry
  (`HKLM\SYSTEM\CurrentControlSet\Enum\PCI` → `LocationInformation` and
  the USB stack's own controller description), 2026-09-12:

  | PCI ID | bus/device/fn (Windows) | USB | xHCI | ports |
  |---|---|---|---|---|
  | `1022:43F7` | 12/0/0 | 3.10 | 1.10, ASMedia subsys (chipset) | 4 |
  | `1022:15B6` | 14/0/3 | 3.10 | 1.20 (Raphael) | 4 |
  | `1022:15B7` | 14/0/4 | 3.10 | 1.20 (Raphael) | 18 |
  | `1022:15B8` | 15/0/0 | 2.0 | 1.20 (Raphael) | 1 |

  The port counts are the ones already recorded in this doc (kernel boot
  log); every boot re-verifies them against HCSPARAMS1.

  `pci_init()` scans bus 0..255, device 0..31, function 0 then 1..7 in
  ascending order, so if RixuriOS reads the same bus numbers the expected
  ctl order is ctl0=`43F7`, ctl1=`15B6`, ctl2=`15B7`, ctl3=`15B8`.
- ctl↔ID mapping: not assumed. Every controller now prints one identity
  line (`xHCI: ctl=N bus=B dev=D fn=F id=VVVV:DDDD hci=0120 slots=N
  ports=N <name> quirks=... profile=ok|mismatch proto=...`) plus a
  `known-bad=` line, and `xhci_dump_ports()` prints `proto=` and
  `known-bad=` per connected port. Windows bus numbers are not guaranteed
  to be the bare-metal numbers RixuriOS reads through MCFG, so the log
  decides, not this table.
- Observed failing ports (rc=4/7, PORTSC 0x331/0x6e1/0xae1/0xee1):
  ctl0 p9/p10/p12/p13/p15/p16, ctl1 p1, ctl2 p1, ctl3 p1.
  Speeds read 1/2/3 (USB2 family); no SuperSpeed device attached yet.
- Open conflict for the next boot log: this list implies an 18-port
  `ctl0`, which the bus-order prediction above does not. Both records
  stand until the identity lines say which is right; nothing in the
  driver depends on either.

### Kernel xHCI hardware profile (Phase H6)

`kernel/usb/xhci_profile.{c,h}` holds this board's per-PCI-ID facts as
pure, host-tested data (`make xhci-profile-test`): expected port count,
expected xHCI interface version, the failing-port list above, and quirk
flags. At init the kernel compares those expectations against each
controller's own `HCSPARAMS1`, `HCIVERSION` and Supported-Protocol walk
and logs `profile=ok` or `profile=mismatch`; the controller always wins
and a quirk is applied only when the silicon confirms it. Exactly one
behavioral quirk is set today: `1022:15B8` is a USB2-only host
controller, so its reset path never takes the USB3 wait-train/warm-reset
branch (which is what wedges such a port at PORTSC 0x331).

Boot-device port selection now scores ports instead of taking the first
connected one: a known-good USB2 port wins (the "aim the keyboard at a
USB2 port" goal), and a port from the failing list above is tried last —
deprioritized, never banned.

## Input situation

- Keyboard: USB (no PS/2 port or PS/2 keyboard available).
- Status: not working yet — H1/H2 port-reset recovery in progress.
  Shell reaches READY on the GOP console; no input path until a USB
  port attaches + enumerates + HID polls. H6 now orders port selection
  (known-good USB2 first) and logs the port on keyboard registration,
  so a boot log shows both the attach attempt and the physical port.

## TBD (waiting on owner)

- One hardware boot log: the `xHCI: ctl=...` identity lines (settles the
  ctl↔PCI-ID mapping and the profile verdicts), the per-port inventory,
  and the `xHCI: attach failed ... PORTSC=` lines. The Windows-side
  bus/device/fn is already recorded above; the RixuriOS side is read from
  this log rather than assumed.
- Which physical rear-panel ports map to which controller/port
  numbers (needed to aim the keyboard at a known-good USB2 port). The
  kernel now prefers USB2 ports and deprioritizes the known-failing ones,
  but the physical-to-port mapping still needs the owner.
- BIOS settings dump if anything USB-related was changed from
  defaults (XHCI handoff, legacy USB support).
