# RixuriOS Master Development Roadmap

**Status:** Living engineering plan  
**Architecture:** x86_64 / AMD64, 64-bit only  
**Kernel:** freestanding C11/C17 + minimal x86_64 assembly  
**Userspace:** Unix-like, musl/POSIX-oriented, dynamically linked  
**GUI:** deliberately last

> **Execution rule:** implement continuously from Phase 1 through Phase 20. Do not stop at compilation. Each phase has explicit checkpoints in [`docs/CHECKPOINTS.md`](CHECKPOINTS.md). A phase cannot be marked complete until its applicable evidence exists.

---

## 0. Master engineering rules

1. 64-bit x86_64 only in the kernel.
2. Specification and ABI/layout review precede implementation.
3. No fake functionality, fake packets, fake disk results, fake keyboard input, fake GPU acceleration or unconditional `PASS` output.
4. Destructive disk operations require explicit confirmation and disposable test media.
5. The kernel is freestanding and never links against glibc, musl, POSIX or Linux kernel APIs.
6. Hardware paths must be real and QEMU/physical devices must remain distinguishable.
7. Every historical regression becomes permanent test coverage where practical.
8. Compilation is only `CP1 BUILD`; it never closes a phase by itself.
9. Every feature gets positive, negative, boundary, timeout and recovery tests appropriate to its risk.
10. Security review is required at every privilege boundary.
11. GUI work cannot displace unfinished kernel, storage, networking, drivers, shell, libc or developer-environment work.
12. Vulkan is a first-class graphics target and must be supported by the underlying memory/device/synchronization design.

## 0.1 Checkpoint protocol

Every phase follows this sequence:

`SPEC → DESIGN → IMPLEMENT → BUILD → UNIT/NEGATIVE TEST → QEMU → INTEGRATION → HARDWARE (when applicable) → REGRESSION → SECURITY → DOCUMENT → RELEASE GATE`

Use the checkpoint identifiers defined in `docs/CHECKPOINTS.md`:

- `CP0 SPEC`
- `CP1 BUILD`
- `CP2 UNIT`
- `CP3 BOOT/QEMU`
- `CP4 INTEGRATION`
- `CP5 HARDWARE`
- `CP6 REGRESSION`
- `CP7 SECURITY`
- `CP8 RELEASE`

A blocked checkpoint is recorded as `BLOCKED`; it is never silently skipped.

---

# Phase 1 — UEFI Boot + Kernel Foundation

### 1A — UEFI
- Validate UEFI tables and function-pointer ABI.
- Locate loaded image/filesystem.
- Load and validate ELF64.
- Allocate/load PT_LOAD segments safely.
- Discover ACPI RSDP and GOP.
- Capture final memory map.
- Retry `ExitBootServices()` when the map key changes.

### 1B — Kernel entry
- SysV x86_64 entry.
- Dedicated stack.
- Serial diagnostics and panic path.
- CPU feature discovery.
- Linker section/layout symbols.

**Checkpoint:** CP0, CP1, CP3, CP6.  
**Done when:** UEFI reaches C17 kernel entry with a validated handoff and no fabricated firmware data.

# Phase 2 — Memory Architecture

### 2A — PMM
- Variable descriptor parsing.
- Usable/reclaimable accounting.
- Kernel/boot/page-table reservations.
- Frame allocation/free.
- DMA-aware physical allocation.

### 2B — VMM
- PML4/5-level capability-aware paging.
- Kernel/user address spaces.
- Map/unmap/protect/translate.
- Huge pages.
- NX, WP, SMEP/SMAP where supported.
- TLB shootdown architecture.
- Page-fault diagnostics.

### 2C — Heap
- Early allocator.
- Size classes/slabs.
- Guard/debug allocation modes.
- Reclaim/free architecture.

**Checkpoint:** CP0–CP3, CP7.  
**Done when:** memory operations are deterministic, permission-aware and isolated between kernel and user mappings.

# Phase 3 — CPU, Interrupts, APIC and SMP

- Correct GDT/TSS/IST.
- Complete IDT/trap-frame model.
- Exception decoding.
- Legacy PIC handling.
- ACPI MADT parsing.
- IOAPIC routing and interrupt-source overrides.
- LAPIC/x2APIC support where appropriate.
- PIT/HPET/APIC timer selection.
- CPU topology.
- AP startup.
- Per-CPU data.
- Spinlocks, mutexes, rwlocks.
- Wait queues and deferred work.
- Interrupt-context restrictions.

**Checkpoint:** CP0–CP3, CP5, CP6.  
**Done when:** timer interrupts work, IRQs route correctly and every discovered CPU can safely initialize.

# Phase 4 — Processes, Threads, Scheduler and Syscalls

### Process model
- Process/address-space objects.
- Kernel/user threads.
- Credentials.
- Parent/child tree.
- Process groups/sessions.
- CPU context.
- Kernel stacks and TSS switching.

### Scheduler
- Preemptive scheduling.
- Per-CPU queues.
- Priority/fairness.
- Sleep/wakeup.
- Timers.
- SMP load balancing.
- Idle threads.

### Syscalls
- Stable syscall ABI.
- SYSCALL/SYSRET or interrupt entry with safe user transitions.
- User-pointer validation.
- Copy-in/out.
- FD tables.
- `read/write/open/close/ioctl`.
- `fork/clone/exec/wait/exit`.
- Signals.
- `mmap/munmap/mprotect`.
- `poll`/event waits.

**Checkpoint:** CP2–CP4, CP7.  
**Done when:** a genuine user process is scheduled, invokes a syscall, performs validated I/O and exits with a status.

# Phase 5 — ELF64 + Dynamic Userspace Runtime

- Robust ELF loader.
- PIE/non-PIE policy.
- User stack and ABI alignment.
- `argc/argv/envp/auxv`.
- VDSO-style interfaces.
- Shared-object mapping.
- Dynamic linker.
- Relocations.
- TLS.
- Initial process runtime.

**Checkpoint:** CP2–CP4, CP7.  
**Done when:** static and dynamically linked programs can start with correct process ABI state.

# Phase 6 — PCIe, MMIO, MSI/MSI-X and DMA

- PCI/PCIe enumeration.
- Capability lists.
- BAR sizing and mapping.
- MSI/MSI-X.
- DMA API.
- IOMMU architecture.
- Device/driver registration.
- Resource ownership/lifetime.
- Hotplug/reset model.
- ID matching and diagnostics.

Required IDs remain explicit: RTL8125 `10EC:8125`, RX 6800 XT `1002:73BF`, QEMU GPU `1B36:0100`.

**Checkpoint:** CP0–CP3, CP5, CP7.

# Phase 7 — Storage + NVMe

- Block device and BIO layers.
- Scatter/gather.
- Queueing/cache/barriers.
- NVMe reset/enable.
- CAP/VS/CC/CSTS.
- Admin queue.
- Identify controller/namespace.
- I/O queues.
- PRP/SGL.
- Read/write/flush.
- Timeout/recovery.
- Namespace lifecycle.

**Checkpoint:** CP2–CP6.  
**Mandatory regression:** controller-ready, Identify, namespace-online, real read, real write and real flush are separate evidence items.

# Phase 8 — VFS + RixFS

### VFS
- Inode/vnode model.
- Superblocks/mounts.
- Mount namespace.
- Path walking.
- FDs.
- Directory iteration.
- Symlinks/hard links.
- Ownership/permissions.
- `stat`, `rename`, `unlink`, `mkdir`, `rmdir`.
- File locking.

### RixFS
- Versioned on-disk specification.
- Superblock/inodes/directories.
- Allocation metadata.
- Extents/direct blocks.
- Free-space tracking.
- Journal/recovery.
- Checksums/integrity.
- Interrupted-write recovery.

**Checkpoint:** CP2–CP7.  
**Mandatory regression:** bad magic/version/corrupt metadata must error or recover; never silently format.

# Phase 9 — USB/xHCI/HID

- xHCI capability/operational/runtime setup.
- Command/event rings.
- Slots/address/configure endpoint.
- Control/bulk/interrupt transfers.
- Descriptor parsing.
- Port lifecycle/hotplug/recovery.
- HID report parser.
- Keyboard/mouse.

**Checkpoint:** CP2–CP6.  
**Mandatory regressions:** Address Device completion code 11 and real keyboard `0x74` (`t`) input.

# Phase 10 — TTY/PTY/Terminal

- TTY and PTY master/slave.
- Line discipline.
- Canonical/raw mode.
- Echo.
- Terminal size.
- Sessions/controlling terminal.
- Foreground process group.
- Terminal signals.
- ANSI/VT behavior.
- UTF-8.
- Pipes/FIFOs.
- Redirection.
- Flow control.

**Checkpoint:** CP2–CP4.  
**Done when:** a real shell process owns a PTY and job-control semantics work.

# Phase 11 — Rixuri Shell

### Parser
- Lexer/tokenizer.
- Quotes/escaping/comments.
- Operators.
- Variable expansion.
- Globbing.
- Command substitution.
- Here-doc architecture.
- Arithmetic expansion.

### Execution
- PATH.
- Builtins/external commands.
- Pipelines.
- Redirection.
- `&&`, `||`, `;`.
- Subshells/background jobs.
- Process groups/signals.
- Exit status.
- `exec`.

### Interactive
- History.
- Persistent history.
- Line editing.
- Cursor movement.
- Completion.
- Aliases/functions.
- Environment/startup scripts.
- Prompt expansion.
- Job control.

### Scripting
- Conditions/loops/functions.
- Script arguments.
- Traps.
- Reliable error propagation.

**Checkpoint:** CP2–CP4, CP7.  
**Acceptance:** real compositions such as `cat file | grep hello | sort | uniq -c` and `make && ./program >out 2>&1` must execute through real processes/pipes/files.

# Phase 12 — Unix Coreutils

Implement real userspace programs over the real syscall/VFS APIs:

- File: `ls cp mv rm mkdir rmdir touch ln stat`
- Text: `cat head tail wc cut tr sort uniq grep sed` foundation
- Search: `find xargs` foundation
- Process/shell: `env printf echo pwd kill ps`
- System: `uname mount umount df du free dmesg`
- Development: `which true false test` and archive foundations

**Checkpoint:** CP2–CP4. Every utility needs real errors and exit statuses.

# Phase 13 — Users, Groups, Permissions, Authentication

- UID/GID.
- Root model.
- User/group databases.
- Ownership/mode checks.
- `chmod/chown` semantics.
- Login/session.
- Password/auth architecture.
- Privilege transitions.
- Environment isolation.
- Capabilities/least privilege.

**Checkpoint:** CP2–CP4, CP7.

# Phase 14 — Full Networking + RTL8125

### Protocol stack
- Ethernet.
- ARP.
- IPv4.
- ICMP.
- UDP.
- TCP.
- Routing.
- Sockets.
- DNS.
- Interface configuration.

### RTL8125
- PCI match `10EC:8125`.
- MMIO/register initialization.
- TX/RX descriptor rings.
- DMA.
- Interrupts.
- Link/MAC state.
- Counters.
- Reset/recovery.

**Checkpoint:** CP2–CP7.  
**Mandatory regression:** automated tests and manual ping must share the same real network path; timeout cannot be converted into success.

# Phase 15 — libc + musl + POSIX + Linux/GLIBC Compatibility

- Syscall glue.
- `errno`.
- libc process/file/memory/time APIs.
- Signals.
- pthread foundation.
- Sockets.
- TLS.
- Dynamic linker integration.
- POSIX compatibility.
- Linux/glibc-oriented compatibility for selected software.
- Porting test suite.

Architecture remains:

`RixuriOS kernel ABI → RixuriOS libc → POSIX/Unix compatibility → applications`

**Checkpoint:** CP2–CP4, CP7.

# Phase 16 — init + Services + devfs/procfs/sysfs

- First userspace PID 1.
- Service startup/supervision/restart.
- `/dev` device nodes.
- `/proc` process/system information.
- `/sys` device/kernel model.
- Resource accounting.
- Logging integration.
- Clean shutdown/reboot.

**Checkpoint:** CP2–CP4.

# Phase 17 — Audio

- Audio device abstraction.
- HDA-oriented PCI driver architecture.
- PCM.
- DMA/ring buffers.
- Mixer/control API.
- Userspace audio API.
- Hotplug/recovery.

**Checkpoint:** CP2–CP5.

# Phase 18 — Graphics Foundation + Vulkan

### Display
- GOP/framebuffer abstraction.
- Pixel formats/scanout.
- Console diagnostics.

### GPU
- PCI discovery.
- VRAM/GTT/system memory.
- MMIO/DMA.
- Command submission.
- Queues.
- Fences/semaphores/events.
- Resource lifetime.
- Reset/recovery.

### QEMU
- Keep `1B36:0100` distinct from AMD hardware.
- Only advertise verified capabilities.

### AMD target
- Identify RX 6800 XT `1002:73BF`.
- Real VRAM/GTT.
- Command/ring architecture.
- Interrupt/fence handling.
- Submission/scheduling.
- Recovery.

### Vulkan
- Loader.
- ICD boundary.
- Physical/logical devices.
- Queues.
- Memory.
- Images/buffers.
- Descriptor/resource model.
- Synchronization.
- Pipeline/shader interfaces.
- Validation/diagnostic tooling.

**Checkpoint:** CP2–CP7. GUI is still forbidden to become the main workstream.

# Phase 19 — Installer + Build System + Toolchain + Developer Environment

### Installer
- Partition discovery.
- Explicit confirmation.
- Bootloader installation.
- Filesystem creation only after confirmation.
- Recovery/rollback paths.
- Install verification.

### Build/toolchain
- Reproducible cross toolchain.
- Kernel/userspace build separation.
- Sysroot.
- Package/build metadata.
- Image generation.
- Debug/symbol artifacts.

### Developer environment
- Compiler/binutils.
- libc development headers.
- Debugger integration.
- Source/build commands.
- Documentation generation.
- Crash/serial log collection.

**Checkpoint:** CP1–CP3, CP7–CP8.

# Phase 20 — GUI (LAST)

Only begin after the non-GUI operating system is mature.

- Window/compositor architecture.
- Input routing.
- Display management.
- GPU/Vulkan integration.
- Fonts/text.
- Clipboard.
- IPC.
- Desktop/session manager.
- GUI toolkit foundation.
- Accessibility.
- Crash recovery to terminal.
- GUI application model.

**Checkpoint:** CP0–CP8. The recovery console and normal Unix shell remain first-class interfaces.

---

# Master release gates

- **R0 — Bootable:** UEFI → kernel → memory/interrupt foundation.
- **R1 — Multitasking:** real userspace process + scheduler + syscalls.
- **R2 — Storage:** NVMe + VFS + RixFS with recovery.
- **R3 — Unix CLI:** TTY/PTY + shell + coreutils.
- **R4 — Networked OS:** users/auth + TCP/IP + RTL8125.
- **R5 — Developer OS:** libc/musl/POSIX compatibility + toolchain.
- **R6 — Pre-GUI platform:** audio + graphics/Vulkan foundation + complete core services.
- **R7 — Graphical OS:** GUI on top of the already-working platform.

## Definition of done

A phase is not done because a function exists. It is done when its checkpoint evidence demonstrates:

`correctness + real execution + integration + failure handling + regression protection + security + documentation`.

The detailed evidence checklist lives in `docs/CHECKPOINTS.md` and must evolve with the implementation.
