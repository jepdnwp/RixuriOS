# RixuriOS Extended Roadmap — PHASE 61–90

This is an additive expansion of the existing roadmap. Phase 00–60 remains valid except where an item is explicitly superseded by `docs/ROADMAP_CORRECTIONS.md`.

RixuriOS is a **single-user desktop PC OS**. Laptop-only features are not product requirements. ACPI is retained only where required for a normal PC: firmware tables, interrupt routing, PCIe discovery, timers, reboot/poweroff, thermal safety and device initialization.

Every phase requires: SPEC → ABI/DATA MODEL → DESIGN → IMPLEMENT → BUILD → UNIT → NEGATIVE → QEMU → INTEGRATION → HARDWARE → REGRESSION → SECURITY → PERFORMANCE → DOCS → CHECKPOINT.

---

# PHASE 61 — PC Platform Baseline and Hardware Compatibility Matrix
- Define the supported desktop-PC hardware envelope.
- CPU generation and feature compatibility matrix.
- AMD/Intel CPU validation.
- UEFI firmware compatibility classes.
- PCIe topology capture.
- NVMe/SATA/USB/NIC/GPU inventory.
- Unsupported-device reporting.
- BIOS/UEFI quirk database.
- Reproducible hardware inventory reports.
- No generic `supported` flag without evidence.

# PHASE 62 — SATA/AHCI Storage Driver
- AHCI controller discovery.
- HBA reset and initialization.
- command list/FIS handling.
- DMA setup.
- SATA identify.
- read/write/flush.
- NCQ architecture.
- timeout and port reset.
- hotplug where applicable.
- bad-sector/error propagation.
- QEMU and physical SATA qualification.

# PHASE 63 — Generic Block Devices and Storage Multiplexing
- Unified block-device registration.
- NVMe/AHCI/virtual disk adapters.
- partitions and partition-table parsing.
- GPT validation and backup-header handling.
- protective MBR handling.
- block-device naming.
- request scheduling.
- queue depth control.
- device disappearance handling.
- storage diagnostics and health reporting.

# PHASE 64 — Partitioning, Formatting and Disk Administration
- GPT creation/editing.
- partition resize rules.
- filesystem creation tools.
- explicit destructive-operation confirmation.
- dry-run mode.
- disk geometry reporting.
- free-space inspection.
- alignment validation.
- recovery from interrupted partition operations.
- never auto-format an unknown disk.

# PHASE 65 — File I/O Completion and POSIX Semantics
- open flags.
- append and truncation semantics.
- `pread`/`pwrite`.
- `readv`/`writev`.
- `fsync`/`fdatasync`.
- `fcntl` operations.
- advisory locks.
- directory FD operations.
- `O_CLOEXEC` and `O_NONBLOCK`.
- accurate errno behavior.
- concurrent file-offset correctness.

# PHASE 66 — Unix Compatibility Surface Expansion
- Complete path/file APIs selected by the project.
- `access`, `chdir`, `fchdir`, `getcwd`.
- `chmod`, `fchmod`, `chown` policy.
- link/unlink/rename semantics.
- directory stream runtime.
- environment APIs.
- resource-limit APIs.
- hostname/system-information APIs.
- compatibility tests against real applications.
- unsupported interfaces documented instead of falsely emulated.

# PHASE 67 — Shell Completion and Interactive Userland
- robust command parser.
- quoting and escaping.
- environment expansion.
- command substitution.
- pipelines.
- redirections.
- background jobs.
- signals and terminal control.
- history.
- line editing.
- tab completion.
- built-in command framework.

# PHASE 68 — Core Userland Utilities
Implement real utilities rather than demonstration commands:
- `cat`, `cp`, `mv`, `rm`, `mkdir`, `rmdir`.
- `ls`, `find`, `grep`, `sort`, `head`, `tail`.
- `printf`, `echo`, `env`, `pwd`, `kill`.
- `ps`, `mount`, `umount`, `df`, `du`.
- `dmesg`/kernel-log reader.
- `ip`/network diagnostics.
- `shutdown`/`reboot` through authorized interfaces.
- consistent exit status and stderr behavior.

# PHASE 69 — Process Resource Limits and Accounting
- CPU-time accounting.
- address-space limits.
- open-FD limits.
- process/thread limits.
- locked-memory limits if supported.
- per-user accounting.
- kernel-object accounting.
- OOM diagnostics.
- runaway-process protection.
- resource usage API.

# PHASE 70 — Device Model and Driver Lifecycle 2.0
- formal bus/device/driver objects.
- driver matching.
- dependency ordering.
- probe/remove lifecycle.
- reset/recovery callbacks.
- device reference counting.
- power-state-independent core model.
- deferred probing.
- device ownership.
- hotplug events.
- driver diagnostics.

# PHASE 71 — PCIe Advanced Features
- PCI capability parser hardening.
- MSI/MSI-X completion.
- PCIe AER architecture.
- error containment.
- link-status diagnostics.
- BAR conflict detection.
- bus numbering.
- bridge traversal.
- multifunction devices.
- PCI reset mechanisms.
- device-level fault recovery.

# PHASE 72 — IOMMU and DMA Isolation
- AMD IOMMU and Intel VT-d architecture.
- device/domain mapping.
- DMA aperture control.
- identity-vs-translated DMA policy.
- invalidation.
- interrupt remapping where supported.
- DMA fault reporting.
- driver isolation.
- bounce-buffer fallback.
- security tests for malicious DMA assumptions.

# PHASE 73 — USB Device Framework Beyond HID
- USB hub support.
- enumeration tree.
- device/configuration/interface/endpoint objects.
- class-driver registration.
- control transfer framework.
- bulk transfer framework.
- interrupt transfer framework.
- isochronous architecture.
- disconnect/reconnect races.
- device reset/recovery.

# PHASE 74 — USB Mass Storage and Removable Media
- BOT protocol.
- SCSI command layer.
- inquiry/capacity/read/write.
- sense data.
- removable media detection.
- filesystem re-mount behavior.
- safe unplug handling.
- timeout/reset.
- corrupted-media tests.
- physical USB storage qualification.

# PHASE 75 — Networking Driver Completion
- RTL8125 real RX/TX path.
- E1000 virtual and physical validation.
- descriptor ownership.
- interrupt moderation policy.
- DMA mapping.
- ring reset.
- link negotiation reporting.
- MTU configuration.
- packet statistics.
- link-down/recovery.
- stress under sustained traffic.

# PHASE 76 — Network Security and Robustness
- ARP poisoning resistance where applicable.
- malformed IPv4/IPv6 packet handling.
- TCP state-machine hardening.
- socket lifetime races.
- SYN/resource exhaustion policy.
- ephemeral-port allocation.
- firewall architecture if selected.
- per-process socket ownership.
- network syscall fuzzing.
- packet-loss and reordering tests.

# PHASE 77 — IPv6 and Modern Network Features
- IPv6 address configuration.
- neighbor discovery.
- router advertisements.
- link-local addresses.
- dual-stack sockets.
- IPv6 routing.
- ICMPv6.
- PMTU handling.
- IPv6 DNS behavior.
- IPv4/IPv6 compatibility tests.

# PHASE 78 — Timekeeping and Clock Correctness
- monotonic clock validation.
- realtime clock discipline.
- TSC synchronization across CPUs.
- clocksource selection.
- timer drift measurement.
- sleep/wakeup accuracy.
- timeout monotonicity.
- filesystem timestamp correctness.
- timestamp overflow/2038-independent design.
- time-related syscall conformance.

# PHASE 79 — Kernel Debugger and Remote Debug Infrastructure
- panic debugger entry.
- register inspection.
- stack unwinding.
- symbol lookup.
- breakpoint/watchpoint architecture.
- GDB-compatible remote protocol where practical.
- kernel/user address inspection.
- thread inspection.
- deadlock inspection.
- crash-to-debugger workflow.

# PHASE 80 — Kernel Sanitizers and Memory Debugging
- allocation poisoning.
- redzones/guard pages.
- use-after-free detection.
- double-free detection.
- slab consistency checks.
- reference-count diagnostics.
- lock misuse detection.
- interrupt-context assertions.
- usercopy boundary instrumentation.
- debug builds separated from production builds.

# PHASE 81 — Concurrency Verification
- lock dependency graph.
- race-oriented stress tests.
- scheduler perturbation.
- randomized wake ordering.
- interrupt timing injection.
- SMP stress.
- futex contention.
- filesystem concurrent access.
- network concurrent sockets.
- deadlock watchdog.

# PHASE 82 — Long-Running Reliability / Soak Program
- 24-hour QEMU stress.
- multi-day physical-PC stress.
- repeated reboot cycles.
- repeated mount/unmount.
- repeated device reset.
- network soak.
- storage soak.
- process churn.
- memory-pressure soak.
- crash-rate tracking.
- leak-rate tracking.

# PHASE 83 — Application Compatibility Qualification
Create a curated real-application suite:
- static C programs.
- dynamically linked C programs.
- pthread applications.
- terminal applications.
- network clients.
- filesystem-heavy programs.
- build tools.
- text-processing utilities.
- shell scripts.
- representative third-party software.

Each application gets a reproducible PASS/FAIL record and dependency report.

# PHASE 84 — Build-System and Reproducibility 2.0
- clean-room builds.
- pinned compiler/tool versions.
- source archive reproducibility.
- deterministic ELF output where practical.
- deterministic filesystem images.
- build provenance.
- generated-file tracking.
- dependency license inventory.
- offline build mode.
- release artifact hashes.

# PHASE 85 — Package Ecosystem and Repository Infrastructure
- package metadata schema.
- dependency constraints.
- ABI compatibility fields.
- package signatures.
- repository index.
- mirror support.
- atomic installation.
- transaction rollback.
- orphan dependency cleanup.
- package verification before execution.

# PHASE 86 — System Recovery and Forensic Mode
- boot failure diagnosis.
- safe/single-user shell.
- read-only filesystem recovery.
- damaged RixFS inspection.
- kernel crash-log extraction.
- hardware inventory extraction.
- network-disabled recovery mode.
- emergency user creation/recovery policy.
- recovery image integrity verification.
- evidence-preserving diagnostics.

# PHASE 87 — Update, Rollback and Compatibility Policy
- versioned kernel ABI policy.
- userspace ABI compatibility.
- package dependency migration.
- atomic system update.
- rollback point creation.
- interrupted-update recovery.
- bootable previous version.
- database/config migration rollback.
- update verification.
- explicit incompatible-update refusal.

# PHASE 88 — Desktop PC Productization
- desktop login/session startup.
- terminal-first default environment.
- optional graphical session.
- display manager only if justified.
- desktop configuration storage.
- keyboard/mouse configuration.
- multi-monitor configuration.
- application launching.
- clipboard.
- basic notifications.
- clean shutdown/reboot UX.

# PHASE 89 — Release Candidate Engineering
- complete regression matrix.
- zero unexplained kernel panics.
- zero known data-corruption paths.
- no fake hardware PASS records.
- no silent filesystem formatting.
- no unresolved critical privilege bypass.
- boot/install/recovery verification.
- physical-PC qualification report.
- compatibility report.
- performance baseline.
- release blocker list.

# PHASE 90 — RixuriOS 1.0 Qualification and Long-Term Maintenance
### 1.0 gates
- reproducible release artifact.
- signed release metadata.
- documented supported hardware.
- documented unsupported hardware.
- documented syscall ABI.
- documented userspace ABI.
- documented filesystem format.
- recovery procedure.
- installation procedure.
- upgrade/rollback procedure.
- security baseline.
- performance baseline.
- application compatibility report.
- physical hardware evidence.

### Maintenance
- stable branch policy.
- security-fix process.
- regression-test preservation.
- ABI deprecation policy.
- filesystem compatibility policy.
- hardware-quirk maintenance.
- release cadence.
- crash-report triage.
- long-term documentation.

## GLOBAL RULE — What never counts as completion

A printed `OK`, detected PCI device, allocated structure, compiled driver, mocked packet, synthetic keyboard event, test-only ELF loader, fake filesystem, skipped test, or source-level claim is not sufficient evidence. Completion requires the real path, the appropriate negative/recovery tests, and the evidence class required by that subsystem.
