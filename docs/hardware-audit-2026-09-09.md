# RixuriOS hardware audit — 2026-09-09

The target system is an ASUS PRIME B650M-R with an AMD Ryzen 7 7700, 32 GB DDR5, onboard Realtek 2.5Gb Ethernet, AMD chipset USB/xHCI, UEFI AMI firmware, two PCIe 4.0 x4 M.2 slots, one CPU PCIe 4.0 x16 slot, and one chipset PCIe 4.0 x1 slot. ASUS lists UEFI BIOS, PXE/WOL, the Realtek 2.5Gb controller, and the two M.2 slots in its official technical specifications: https://www.asus.com/motherboards-components/motherboards/prime/prime-b650m-r/techspec/.

AMD lists the Ryzen 7 7700 as an 8-core/16-thread Zen 4 AM5 processor with DDR5, PCIe 5.0 CPU connectivity, NVMe support, and integrated Radeon graphics: https://www.amd.com/en/products/processors/desktops/ryzen/7000-series/amd-ryzen-7-7700.html.

The Linux r8169 source confirms the RTL8125 descriptor layout is opts1, opts2, addr and that the RTL8125-specific TX poll register is 0x90. It also confirms RTL8125 RX descriptor high is 0xe8, while 0xe0 is CPlusCmd, and the RTL8125 interrupt registers are 0x38/0x3c: https://codebrowser.dev/linux/linux/drivers/net/ethernet/realtek/r8169_main.c.html.

RixuriOS was using legacy RTL8169 offsets for those RTL8125 operations. The project now uses the RTL8125 offsets, sets single-buffer FS/LS flags, removes the reported RX FCS before protocol parsing, and programs RxMaxSize. RTL8125 host tests and the full regression suite pass; the ISO builds successfully.

Remaining hardware caveat: actual network success still requires a physical test after flashing the rebuilt ISO. If DHCP remains unavailable after the corrected register map, the next diagnostic must capture TX completion/ISR and RX descriptor flags; the remaining likely causes would then be RTL8125 reset/PHY sequencing or PCIe power-state/bus-master state, not DHCP packet construction.
