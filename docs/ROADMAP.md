# RixuriOS Master Development Roadmap

**Status:** Living project plan  
**Architecture:** x86_64 / AMD64, 64-bit only  
**Kernel:** freestanding C11/C17 + minimal x86_64 assembly  
**Userspace:** Unix-like, musl/POSIX-oriented, dynamically linked  
**GUI:** intentionally deferred until the non-GUI operating system is mature

---

## 0. Mission and non-negotiable rules

RixuriOS is intended to become a serious, extensible Unix-like operating system rather than a boot demo. The project must grow from a verified kernel foundation into a usable multi-process userspace with storage, networking, drivers, a powerful shell, development tooling, and eventually a modern graphics stack.

Rules for every phase:

1. **64-bit only.** Target x86_64/AMD64; do not introduce 32-bit compatibility into the kernel architecture.
2. **Specification first.** Before implementation, inspect the relevant project specification and existing code, identify ABI/layout dependencies, make a plan, then implement.
3. **No fake functionality.** A printed `PASS`, unconditional success return, fabricated packet/disk/keyboard/GPU result, or stub that pretends to perform real work is not a test or feature.
4. **No silent destructive operations.** Formatting, partitioning, overwriting disks, or installer destruction requires explicit confirmation and must have safe failure paths.
5. **Kernel/userspace boundary is real.** The kernel remains freestanding and must not depend on glibc, musl, POSIX, or Linux kernel APIs. Those belong in userspace compatibility layers.
6. **Hardware paths must be real.** QEMU devices and physical devices are separate targets and must be identified correctly.
7. **Regression prevention is mandatory.** Historical failures become permanent regression tests where practical.
8. **Do not advance a phase merely because code compiles.** A phase advances only after its acceptance gates are satisfied.
9. **GUI is last.** No desktop/window system work should displace unfinished kernel, driver, Unix userspace, shell, networking, storage, or developer-environment work.
10. **Vulkan is a first-class long-term graphics target.** The graphics architecture must be designed early enough that Vulkan does not require replacing the kernel/device/memory model later.

---

# Phase 1 — Boot and Kernel Foundation

### 1.1 UEFI loader
- Correct UEFI ABI/function-table layouts.
- Locate the loaded image and filesystem.
- Load the kernel ELF64 image from the boot volume.
- Validate ELF magic, class, endianness, machine, program-header size/count, bounds and overflow.
- Validate `PT_LOAD` segments, alignment and address ranges.
- Allocate physical pages for kernel segments.
- Establish a stable boot handoff structure.
- Discover ACPI RSDP.
- Discover UEFI GOP framebuffer metadata for future graphics work.
- Capture the final memory map and exit boot services correctly, including retry handling when the map key changes.

### 1.2 Kernel entry
- SysV x86_64 ABI kernel entry.
- Dedicated early stack.
- Serial logging.
- Panic path.
- Early CPU feature discovery.
- Kernel section/layout symbols.

### Gate
The machine must reach C17 kernel entry using a validated UEFI handoff without relying on undefined UEFI structure offsets or fake memory-map data.

---

# Phase 2 — Physical and Virtual Memory

### 2.1 PMM
- Parse variable-size UEFI descriptors safely.
- Correctly account for conventional/reclaimable memory.
- Reserve kernel image, boot information, page tables and firmware-owned regions.
- Physical frame allocator.
- Free-frame accounting.
- Alignment and overflow-safe range operations.
- DMA-capable allocation primitives.

### 2.2 VMM
- Four/five-level x86_64 paging as required by discovered CPU capabilities.
- Page-table allocator.
- Kernel virtual address space.
- Map/unmap/protect operations.
- Huge-page support where appropriate.
- User/supervisor and writable/executable permission enforcement.
- NX/WP/SMEP/SMAP support where safely available.
- TLB invalidation.
- Page-fault diagnostics.

### 2.3 Kernel heap
- Early allocator.
- General kernel heap.
- Slab/size-class allocator architecture.
- Guard/debug modes for development.

### Gate
Memory allocation, mapping, unmapping and faults are deterministic, permission-aware and independently testable.

---

# Phase 3 — CPU, Exceptions, Interrupts and SMP

- GDT and segment setup.
- IDT and exception stubs.
- Full CPU exception reporting.
- PIC disable/legacy handling as needed.
- Local APIC.
- IOAPIC.
- IRQ routing.
- High-resolution/periodic timer source.
- CPU feature and topology discovery.
- AP startup and SMP initialization.
- Per-CPU state.
- Interrupt-safe locking primitives.
- Spinlocks, mutexes, rwlocks and wait queues.
- Interrupt-context rules and deferred work.

### Gate
Exceptions are decoded, IRQs route correctly, multiple CPUs can initialize safely, and synchronization primitives have documented context restrictions.

---

# Phase 4 — Process, Thread and Syscall Core

### Process/thread model
- Kernel thread abstraction.
- User thread abstraction.
- Address-space object.
- Process credentials.
- Parent/child relationships.
- Process groups and sessions.
- CPU context structures.
- Context switching.

### Scheduler
- Preemptive scheduler.
- Per-CPU run queues.
- Priority/fairness model.
- Sleep/wakeup.
- Timers/timeouts.
- Load balancing for SMP.

### Syscalls
- Stable syscall numbering/ABI policy.
- User pointer validation.
- Copy-in/copy-out helpers.
- Error/errno model.
- File descriptor table.
- `read`, `write`, `open`, `close`, `ioctl` foundations.
- `fork`/clone-style process creation architecture.
- `exec`.
- `wait`.
- `exit`.
- `kill`/signals foundation.
- `mmap`, `munmap`, memory protection.
- `poll`/event waiting architecture.

### Gate
A real user process can be created, scheduled, enter kernel mode through a syscall, perform validated I/O, and exit with a status.

---

# Phase 5 — ELF64 and Userspace Runtime

- Robust ELF64 loader.
- PIE/non-PIE policy.
- ASLR architecture where practical.
- User stack construction.
- `argc/argv/envp`.
- Auxiliary vector.
- VDSO-like fast userspace interfaces where useful.
- Shared-object mapping.
- Dynamic linker ABI.
- TLS architecture.
- Initial userspace runtime.

### Gate
A real dynamically linked userspace executable can start with correct arguments/environment and perform syscalls through the defined ABI.

---

# Phase 6 — PCI, MMIO and DMA Device Model

- PCI configuration-space access.
- PCIe enumeration.
- BAR discovery.
- MMIO mapping.
- Interrupt capabilities/MSI/MSI-X.
- DMA/IOMMU-aware interfaces.
- Device/driver registration model.
- Device lifecycle and teardown.
- Resource ownership.
- Device IDs and matching.
- Driver logging and diagnostics.

Target IDs that must remain explicit in tests:
- RTL8125: `10EC:8125`.
- RX 6800 XT: `1002:73BF`.
- QEMU virtual GPU target: `1B36:0100`.

---

# Phase 7 — Storage and NVMe

### Storage abstraction
- Block device API.
- BIO/request abstraction.
- Scatter/gather I/O.
- Request queues.
- Completion handling.
- Cache/buffer layer.
- Flush/barrier semantics.
- Error propagation.

### NVMe
- PCI discovery.
- Controller reset/enable.
- CAP/VS/CC/CSTS handling.
- Admin queue.
- Identify controller.
- Identify namespace.
- I/O submission/completion queues.
- PRP/SGL handling as appropriate.
- Read/write.
- Flush.
- Timeout/recovery.
- Namespace lifecycle.

### Regression gate
Controller-ready, Identify, namespace-online, real read, real write and real flush are separate tests. No self-test may report success without exercising the real I/O path.

---

# Phase 8 — VFS and RixFS

### VFS
- vnode/inode-like object model.
- Superblock/filesystem instances.
- Mount namespace.
- Path resolution.
- File descriptors.
- Directory iteration.
- Open flags and modes.
- File locks where required.
- Symlinks/hard links.
- Permissions and ownership hooks.
- `stat` family.
- `rename`, `unlink`, `mkdir`, `rmdir`.

### RixFS
- On-disk format specification.
- Superblock.
- Allocation metadata.
- Inodes.
- Direct/indirect or extent-based data layout.
- Directories.
- Free-space management.
- Journaling/recovery architecture.
- Checksums/integrity where appropriate.
- Mount validation.
- Recovery from interrupted writes.

**Critical rule:** invalid magic/version/corrupt metadata must cause a clear error or recovery path. Never silently format a disk.

---

# Phase 9 — USB / xHCI / HID

- xHCI controller discovery.
- Capability/operational/runtime registers.
- Command ring.
- Event ring.
- Device slots.
- Address Device.
- Configure Endpoint.
- Control/bulk/interrupt transfers.
- USB descriptors.
- Port/device lifecycle.
- Reset/recovery.
- HID parser.
- Keyboard driver.
- Mouse driver.
- Hotplug architecture.

### Regression gate
The historical xHCI Address Device completion code 11 issue must have a dedicated diagnostic path. USB keyboard timeout followed by real `0x74` (`t`) input must remain a regression case.

---

# Phase 10 — TTY, PTY and Terminal Subsystem

This phase is intentionally large because the command-line environment is a core product, not a placeholder.

- TTY abstraction.
- PTY master/slave.
- Line discipline.
- Canonical/raw modes.
- Echo and signal-generating characters.
- Terminal window size.
- Job-control semantics.
- Foreground process group.
- Session/controlling-terminal model.
- ANSI/VT-style terminal escape handling.
- UTF-8 handling.
- stdin/stdout/stderr.
- Pipes.
- Named pipes where supported.
- Redirection.
- Terminal resize events.
- Input buffering.
- Output flow control.

### Gate
An interactive shell can run as a genuine foreground process, launch children, connect pipes/redirections and receive terminal-generated signals.

---

# Phase 11 — Rixuri Shell

The shell must evolve into a substantial Unix-like shell rather than a single-command parser.

### Parser
- Tokenizer.
- Quoting: single, double and escaping.
- Operators.
- Newlines and command separators.
- Comments.
- Here-documents architecture.
- Command substitution.
- Arithmetic expansion architecture.
- Variable expansion.
- Globbing.
- Brace expansion where appropriate.

### Execution
- PATH lookup.
- Builtins.
- External programs.
- Pipelines.
- Redirections.
- `&&`, `||`, `;`.
- Subshells.
- Background jobs.
- Process groups.
- Signals.
- Exit-status propagation.
- `exec` replacement.

### Interactive features
- Command history.
- Persistent history file.
- Cursor movement.
- Line editing.
- Tab completion.
- Programmable completion architecture.
- Aliases.
- Shell functions.
- Environment management.
- Startup scripts.
- Prompt expansion.
- Job listing/control.

### Scripting
- Reliable exit status.
- Variables/arrays as supported by the shell design.
- Conditions.
- Loops.
- Functions.
- Trap/signal handling.
- Script arguments.
- Redirection/pipelines in scripts.
- Error handling modes.

Example target capabilities:

```sh
cat file.txt | grep "hello" | sort | uniq -c
make && ./program >output.txt 2>&1
```

These examples are acceptance targets, not fake demo commands.

---

# Phase 12 — Unix Core Utilities

Build real userspace utilities on the real syscall/VFS/process APIs.

Initial groups:

- Files: `ls`, `cp`, `mv`, `rm`, `mkdir`, `rmdir`, `touch`, `ln`, `stat`.
- Text: `cat`, `head`, `tail`, `wc`, `cut`, `tr`, `sort`, `uniq`, `grep`, `sed`-like foundation.
- Search: `find`, `xargs`-like functionality.
- Shell/process: `env`, `printf`, `echo`, `pwd`, `kill`, `ps`.
- System: `uname`, `mount`, `umount`, `df`, `du`, `free`, `dmesg`.
- Development: `which`, `true`, `false`, `test`, archive/tooling foundations.

Utilities must report real errors and exit statuses.

---

# Phase 13 — Users, Groups, Permissions and Authentication

- UID/GID.
- Root/superuser model.
- User/group databases.
- File ownership.
- Permission checks.
- `chmod`/`chown` semantics.
- Login/session model.
- Password/authentication architecture.
- Privilege transitions.
- Environment isolation.
- Capability/least-privilege architecture where useful.

---

# Phase 14 — Networking

### Kernel/network stack
- Network device abstraction.
- Ethernet.
- ARP.
- IPv4.
- ICMP.
- UDP.
- TCP.
- Routing.
- Interfaces.
- Socket API.
- DNS resolver architecture.
- Network configuration.

### RTL8125
- PCI discovery using `10EC:8125`.
- MMIO/register initialization.
- Descriptor rings.
- DMA.
- TX/RX.
- Interrupts.
- Link state.
- MAC configuration.
- Packet counters.
- Recovery/reset.

### Gate
Manual network operations and automated self-tests must use the same actual network path. A self-test timeout must not be masked by an unrelated fabricated success.

---

# Phase 15 — libc, musl, POSIX and Linux/GLIBC-Oriented Compatibility

- libc syscall glue.
- `errno`.
- Process/thread primitives.
- File APIs.
- Memory APIs.
- Time APIs.
- Signals.
- pthread foundation.
- Sockets.
- Dynamic linking.
- TLS.
- POSIX compatibility layer.
- Linux/glibc-oriented compatibility where it materially helps port software.

Maintain a clear distinction between:

```text
RixuriOS kernel ABI
        ↓
RixuriOS libc/syscall layer
        ↓
POSIX / Unix compatibility
        ↓
ported applications
```

---

# Phase 16 — init, Services, devfs/procfs/sysfs

- First userspace process/init.
- Boot-time service startup.
- Service supervision/restart architecture.
- Device nodes.
- `/dev`.
- `/proc` process/system views.
- `/sys` device/kernel views.
- System information interfaces.
- Resource accounting hooks.
- Logging service integration.
- Clean shutdown/reboot architecture.

---

# Phase 17 — Audio and Pre-GUI Hardware Services

- Audio device abstraction.
- PCI audio discovery.
- AMD/HDA-oriented architecture.
- PCM buffers.
- DMA/ring-buffer handling.
- Mixer/control model.
- Userspace audio API foundation.
- Device hotplug/recovery.

No GUI dependency is allowed for basic audio/device testing.

---

# Phase 18 — Graphics Foundation and Vulkan

**Vulkan is a mandatory long-term target.** The architecture must be prepared before GUI implementation.

### 18.1 Display foundation
- UEFI GOP handoff.
- Framebuffer abstraction.
- Pixel formats.
- Scanout metadata.
- Console/framebuffer diagnostics.

### 18.2 GPU/device layer
- PCI GPU enumeration.
- GPU memory model.
- VRAM/system-memory allocation.
- MMIO.
- DMA.
- Interrupts.
- GPU reset/recovery.
- Command submission abstraction.
- Queue model.
- Synchronization primitives.
- Fences/semaphores/events.
- Buffer/image abstraction.
- Resource lifetime management.

### 18.3 QEMU GPU
- Identify `1B36:0100` separately from physical AMD hardware.
- Implement only verified capabilities.
- Keep software fallback/debug paths explicit.

### 18.4 AMD GPU target
- Identify RX 6800 XT `1002:73BF`.
- Design driver architecture around real hardware requirements.
- VRAM/GTT management.
- Command processor/ring architecture as supported.
- Interrupt/fence handling.
- GPU scheduling/submission model.
- Reset/recovery.

### 18.5 Vulkan userspace architecture
- Vulkan loader.
- ICD boundary.
- Physical device enumeration.
- Logical device.
- Queue families.
- Command pools/buffers.
- Synchronization.
- Device memory.
- Buffers/images.
- Image views/samplers.
- Descriptor infrastructure.
- Shader modules.
- Pipeline architecture.
- Render/presentation model.
- Swapchain.
- Surface support.
- Validation/debug infrastructure.

**No fake Vulkan acceleration.** If a feature is unavailable, expose that limitation explicitly instead of pretending the hardware supports it.

---

# Phase 19 — Developer Environment and Mayo

- Native compiler/toolchain strategy.
- Assembler/linker.
- Build system.
- Package/build workflow.
- Headers and SDK structure.
- Debugging facilities.
- Kernel symbol tooling.
- Crash dumps/backtraces where possible.
- Developer shell utilities.
- Mayo text editor creation(nano like).
- Reproducible builds.
- Cross-compilation workflow.
- Host-to-RixuriOS development workflow.

The OS should eventually be capable of supporting serious software development from its own terminal environment.

---

# Phase 20 — Installer and Boot Media

- UEFI removable-media layout.
- `EFI/BOOT/BOOTX64.EFI`.
- Kernel placement.
- FAT ESP/image creation.
- GPT handling.
- Disk discovery.
- Installer UI in terminal first.
- Explicit destructive confirmations.
- Safe dry-run mode.
- Error recovery.
- Install logs.
- Boot entry management where supported.
- USB installation validation.

No automatic formatting or destructive fallback.

---

# Phase 21 — Security Hardening

- W^X / NX.
- Kernel/user isolation.
- SMEP/SMAP where available.
- Stack protection strategy compatible with freestanding kernel constraints.
- Syscall validation.
- Integer/size overflow audits.
- DMA/IOMMU strategy.
- Privilege separation.
- Capability model where appropriate.
- Secure filesystem validation.
- Driver isolation boundaries where feasible.
- Audit logging.
- Secure defaults.

---

# Phase 22 — Testing, QEMU, Real Hardware and Regression

### Automated
- Build checks.
- ELF validation tests.
- Unit tests for portable kernel components.
- Syscall tests.
- VFS/filesystem tests.
- Shell parser/executor tests.
- Network protocol tests.
- Driver tests.

### QEMU
- Boot tests.
- Serial log assertions.
- Storage tests.
- USB tests.
- Network tests.
- GPU identification/tests.
- Userspace process tests.
- Shell tests.

### Real target
Project hardware target includes the specified AMD Ryzen 7 7700 platform, RX 6800 XT, AMD iGPU, XPG GAMMIX S70 BLADE NVMe, RTL8125 networking, AMD xHCI and ASUS platform components.

### Permanent regressions
- UEFI `GetMemoryMap` invalid-opcode/#UD investigation path.
- xHCI Address Device completion code 11.
- USB keyboard timeout / real key `0x74`.
- NVMe readiness/Identify/read/write/flush.
- GPU ID distinction: QEMU `1B36:0100` vs RX 6800 XT `1002:73BF`.
- Filesystem bad magic/version handling.
- RTL8125 self-test using the real network path.
- Compiler warnings and bounds errors.

Never mark a regression fixed without reproducing or otherwise validating the relevant behavior.

---

# Phase 23 — Performance and Reliability

- Scheduler benchmarks.
- Syscall latency.
- Context-switch measurements.
- Memory allocator benchmarks.
- VFS/storage throughput and latency.
- NVMe queue-depth tests.
- Network throughput/latency.
- TCP stress.
- USB throughput.
- GPU submission/presentation metrics.
- Vulkan workload benchmarks once available.
- Memory leak detection.
- Lock contention analysis.
- Boot-time measurements.
- Long-running stability tests.

---

# Phase 24 — Pre-GUI Operating System Completion Gate

GUI work is blocked until all of the following are substantially functional:

- UEFI boot.
- Stable kernel memory management.
- Interrupts/APIC/SMP.
- Scheduler/processes/threads.
- Syscalls.
- ELF userspace.
- VFS/RixFS.
- NVMe/storage.
- USB/xHCI/HID.
- TTY/PTY.
- Advanced shell.
- Core utilities.
- Users/groups/permissions.
- Network stack.
- RTL8125.
- libc/musl/POSIX foundations.
- Dynamic linking.
- init/services.
- devfs/procfs/sysfs.
- Developer environment.
- Installer.
- Logging/debugging.
- QEMU regression suite.
- Real-hardware validation where hardware is available.
- Security baseline.
- Graphics/GPU architecture sufficient to support the future Vulkan stack.

The intended result is already a useful Unix-like operating system **without a GUI**.

---

# Phase 25 — GUI / Desktop (explicitly deferred)

Only after Phase 24:

- Display server/compositor.
- Window management.
- Input routing.
- GPU-accelerated rendering.
- Vulkan presentation.
- Desktop/session management.
- Fonts/text rendering.
- Clipboard.
- Accessibility architecture.
- Applications and desktop services.

The GUI must consume the already-tested OS, GPU and Vulkan layers instead of forcing a redesign of the foundations.

---

# Execution Order From Current Repository State

At the time this roadmap was introduced, the repository has reached the early UEFI loader/kernel-entry foundation. Therefore implementation proceeds in this order:

1. Repair and harden `boot/efi_main.c` completely.
2. Finalize boot handoff including memory map, ACPI RSDP and GOP metadata.
3. Implement PMM correctly from the final UEFI memory map.
4. Replace the placeholder VMM with a real paging implementation.
5. Add kernel heap and memory diagnostics.
6. Implement GDT/IDT/exceptions/APIC/timer.
7. Implement SMP and synchronization.
8. Implement process/thread/scheduler machinery.
9. Implement syscall ABI and user address spaces.
10. Implement ELF64 userspace and dynamic-loader foundation.
11. Build PCI/DMA/device infrastructure.
12. Implement NVMe and storage abstraction.
13. Implement VFS/RixFS.
14. Implement xHCI/HID/keyboard.
15. Implement TTY/PTY.
16. Build the advanced Rixuri Shell.
17. Build the Unix core utilities.
18. Add users/groups/permissions.
19. Build network stack and RTL8125.
20. Integrate musl/POSIX/dynamic linking.
21. Add init/services/devfs/procfs/sysfs.
22. Add audio and pre-GUI hardware services.
23. Build graphics foundation and Vulkan-oriented GPU stack.
24. Build developer environment/Mayo.
25. Build terminal installer/boot media.
26. Harden security.
27. Expand QEMU + real-hardware + regression/performance testing.
28. Pass the pre-GUI completion gate.
29. **Only then begin GUI/desktop work.**

---

# Definition of Done

RixuriOS is not considered mature because it boots once. A mature milestone means:

- The implementation is real.
- The ABI and data structures are documented.
- Errors propagate correctly.
- Tests exercise the actual subsystem.
- Regressions are covered.
- QEMU behavior is reproducible.
- Real hardware behavior is validated where available.
- No fake PASS paths exist.
- No destructive operation happens without explicit authorization.
- Userspace can meaningfully use the subsystem.
- The subsystem does not block the next architectural layer.

**This roadmap is the authoritative implementation order unless a new architectural discovery requires an explicit documented revision.**
