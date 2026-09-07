# RixuriOS Engineering Checkpoints

This file defines the evidence required before the roadmap may advance. A checkpoint is a gate, not a progress percentage.

## Checkpoint states

- `CP0 SPEC` — requirements, ABI/layouts and invariants reviewed.
- `CP1 BUILD` — source compiles and links with warnings treated as errors.
- `CP2 UNIT` — deterministic host-side or freestanding tests cover edge cases.
- `CP3 BOOT` — code is exercised from the real boot path in QEMU where applicable.
- `CP4 INTEGRATION` — subsystem works through its real neighboring interfaces.
- `CP5 HARDWARE` — physical-target behavior is verified when the phase targets hardware.
- `CP6 REGRESSION` — historical failures have permanent regression coverage.
- `CP7 SECURITY` — privilege, bounds, lifetime and failure-path review is complete.
- `CP8 RELEASE` — documentation, diagnostics and reproducible build artifacts are complete.

A phase is **COMPLETE** only when all applicable checkpoints are satisfied. `BUILD` alone never closes a phase.

## Evidence rules

1. Every checkpoint must name the artifact, test, log, or source invariant that proves it.
2. A serial message saying `PASS` is never evidence by itself.
3. Tests must exercise the same implementation path users or hardware use.
4. Negative tests are mandatory for malformed input, timeouts, invalid permissions and resource exhaustion.
5. Hardware-specific tests must record PCI IDs, firmware assumptions and observed status codes.
6. Destructive storage tests must use an explicitly designated test device/image.
7. If evidence is unavailable, the checkpoint remains `BLOCKED`, not `COMPLETE`.

## Phase matrix

### Phase 1 — Boot
- CP0: UEFI ABI, ELF64 and handoff layouts reviewed.
- CP1: loader and kernel link cleanly.
- CP3: removable `BOOTX64.EFI` loads `kernel.elf` and reaches C entry.
- CP6: EBS/memory-map and historical UEFI failure paths covered.

### Phase 2 — Memory
- CP1: PMM/VMM/heap build cleanly.
- CP2: descriptor parsing, overflow, alignment and permission tests.
- CP3: page mapping/unmapping and page-fault diagnostics exercised in QEMU.
- CP7: W^X, user/kernel isolation and allocator lifetime rules reviewed.

### Phase 3 — Interrupts/SMP
- CP2: exception-frame and locking tests.
- CP3: LAPIC/IOAPIC/timer IRQ delivery in QEMU.
- CP5: CPU topology and AP startup on the target platform.
- CP6: IRQ routing and historical interrupt regressions.

### Phase 4 — Processes/Syscalls
- CP2: user-pointer, FD and scheduler edge cases.
- CP3: user process enters/exits through the syscall ABI.
- CP4: scheduler + address space + VFS + signals operate together.
- CP7: syscall privilege/bounds review.

### Phase 5 — ELF/Runtime
- CP2: malformed ELF and segment-boundary tests.
- CP3: static and dynamic executables start with argc/argv/envp.
- CP4: dynamic linker, TLS and libc startup integration.
- CP7: executable permissions and ASLR invariants.

### Phase 6 — PCI/DMA
- CP2: config-space/BAR/MSI parsing tests.
- CP3: QEMU PCI enumeration.
- CP5: physical device discovery.
- CP7: DMA ownership and MMIO bounds review.

### Phase 7 — NVMe
- CP2: queue/PRP/error-path tests.
- CP3: controller reset/enable and Identify in QEMU.
- CP5: target NVMe discovery and I/O.
- CP6: separate real read/write/flush regression tests.

### Phase 8 — VFS/RixFS
- CP2: path resolution, permissions and malformed filesystem tests.
- CP3: mount/read/write/readdir/unlink/rename in a disposable image.
- CP4: VFS ↔ block ↔ RixFS integration.
- CP6: bad magic/version never triggers formatting.
- CP7: crash-recovery and corruption handling review.

### Phase 9 — USB/HID
- CP2: descriptor/parser and transfer-state tests.
- CP3: xHCI controller and keyboard in QEMU.
- CP5: physical xHCI/HID operation.
- CP6: Address Device completion 11 and keyboard `0x74` regressions.

### Phase 10 — TTY/PTY
- CP2: ring-buffer, canonical/raw and signal-generation tests.
- CP3: interactive TTY/PTY session.
- CP4: shell pipelines, redirection and job control over PTY.

### Phase 11 — Shell
- CP2: lexer/parser expansion and quoting tests.
- CP3: interactive command execution.
- CP4: pipelines, redirection, jobs, signals, history and completion.
- CP7: environment/command-substitution/scripting safety review.

### Phase 12 — Coreutils
- CP2: utility argument/error/exit-status tests.
- CP3: utilities execute against real VFS and process APIs.
- CP4: utilities compose through shell pipelines.

### Phase 13 — Users/Auth
- CP2: permission matrix tests.
- CP3: login/session and privilege transitions.
- CP4: filesystem, process and network authorization integration.
- CP7: least-privilege and credential lifetime review.

### Phase 14 — Networking
- CP2: Ethernet/ARP/IP/ICMP/UDP/TCP protocol tests.
- CP3: QEMU virtual networking path.
- CP5: RTL8125 `10EC:8125` real link/TX/RX path.
- CP6: manual ping and automated self-test use the same path.

### Phase 15 — libc/Compatibility
- CP2: syscall/libc ABI tests.
- CP3: dynamically linked POSIX-oriented programs.
- CP4: compatibility test suite for selected Unix/Linux software.
- CP7: kernel remains independent of libc/POSIX/Linux kernel APIs.

### Phase 16 — init/services/pseudo-fs
- CP2: service state-machine and proc/sys/dev consistency tests.
- CP3: first userspace init starts services.
- CP4: clean shutdown/reboot and service supervision.

### Phase 17 — Audio
- CP2: PCM/ring-buffer tests.
- CP3: virtual audio device path where available.
- CP5: physical audio playback/capture path.

### Phase 18 — Graphics/Vulkan foundation
- CP2: resource lifetime, synchronization and command-model tests.
- CP3: GOP/framebuffer console.
- CP5: hardware identification including RX 6800 XT `1002:73BF`.
- CP6: QEMU GPU `1B36:0100` kept separate from AMD driver paths.
- CP7: GPU memory isolation and reset/recovery review.

### Phase 19 — Installer/Build/Toolchain/Developer Environment
- CP1: reproducible toolchain/build artifacts.
- CP2: package/image generation and negative installer tests.
- CP3: install into a disposable image and boot it.
- CP7: explicit destructive-action confirmation.
- CP8: documented developer workflow.

### Phase 20 — GUI (last)
- CP0–CP8 from prior phases must already be closed unless a documented dependency is intentionally deferred.
- CP2: compositor/window/input/resource tests.
- CP3: graphical session boots without hiding terminal/recovery paths.
- CP4: GUI apps use the normal process, VFS, IPC, audio, network and graphics APIs.
- CP8: recovery console remains available.

## Release checkpoint ladder

`R0` bootable kernel → `R1` protected multitasking → `R2` real storage/VFS → `R3` interactive Unix shell → `R4` networking/users → `R5` developer-capable Unix-like system → `R6` hardware-complete pre-GUI platform → `R7` graphical platform.

No release label may be claimed until its preceding checkpoint evidence is archived in `docs/` or an equivalent reproducible test artifact.


## Latest checkpoint evidence — 2026-09-07

The Phase 8 storage integration gate has new disposable-image evidence: `make -j2 all CROSS=x86_64-linux-gnu-`, `make image CROSS=x86_64-linux-gnu-`, `make test CROSS=x86_64-linux-gnu- HOST_CC=gcc`, `python3 scripts/qemu_cp_mv_edge_test.py`, and `python3 scripts/qemu_phase19_extended_test.py` completed successfully. The QEMU path exercised RixFS through NVMe, VFS, syscalls, libc, shell, and real userspace utilities. Rename coverage includes inode preservation, overwrite, cross-directory movement, reusable deleted slots, and fail-closed directory collision handling. This closes the observed CP3/CP4/CP6 scenarios for regular-file rename and the listed coreutils flows, not the full crash-recovery or hardware gate.

The Phase 13/20 credential gate also passed `make phase20-test CROSS=x86_64-linux-gnu-`. The run covered UID/GID and ACL policy, capability delegation, kill behavior, session lifecycle, protected shadow access, account add/remove, password rotation, new-password verification, old-password rejection, and login/logout. This is QEMU-validated for the observed policy matrix; physical-device, broader authorization, and injected storage-failure evidence remain open.


## Phase 20 closure review — 2026-09-07

The bounded Phase 20 implementation now has QEMU evidence for owner/group/other access, path-search denial, supplementary groups, saved IDs, ACLs, capabilities and delegation, audit identity, set-ID transitions, tainted-environment sanitization, session lifecycle, account lock/unlock, invalid-account no-mutation behavior, password rotation and login. Strict build and host regressions pass.

CP7 and CP8 remain open for the phase as a whole. The remaining blockers are injected mid-commit power-loss/crash testing for the account store and journal, physical NVMe/security-target evidence, a complete interactive login/lockout service, and a final review of the broader authorization matrix. These cannot be honestly claimed from the current disposable QEMU environment; the shell prompt therefore remains unchanged.
