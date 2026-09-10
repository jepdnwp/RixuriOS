# Roadmap Corrections — PC Product Scope

The project is a **single-user desktop PC operating system**. Laptop-specific product features are not roadmap requirements.

## Phase 50 correction

The former laptop-oriented items are superseded for product planning.

Keep only the PC-relevant platform work:

- ACPI table parsing required for firmware/platform discovery.
- FADT/MADT/HPET/MCFG access where required.
- reboot and poweroff.
- CPU idle states where useful.
- thermal safety where exposed by the PC firmware.
- PCIe/device initialization dependencies.
- suspend/resume only if the desktop target actually requires it.
- no battery, lid, laptop fan, laptop power-profile or laptop-specific UX requirements.

## Product priorities

1. Real x86_64 desktop PC hardware.
2. Stable kernel and memory management.
3. SMP and preemptive scheduling.
4. Real storage and filesystem reliability.
5. Real Ethernet/networking.
6. Complete process/thread/signal model.
7. Dynamic ELF/TLS and real musl integration.
8. Secure privilege boundaries.
9. Recovery and crash diagnostics.
10. Developer/toolchain/package ecosystem.
11. Physical hardware qualification.
12. GUI only after the terminal-first OS is genuinely reliable.

## Scope rule

Do not add features merely because Linux/Windows has them. Add a subsystem when it materially improves the single-user PC OS, is architecturally justified, and can be implemented and tested honestly.
