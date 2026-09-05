# RixuriOS Implementation Playbook

This document explains **how to build the roadmap**, not merely what to build. It is the operating manual for coding agents and human contributors.

## 1. Task lifecycle

For every task:

1. Read the relevant phase and all dependencies.
2. Inspect the existing source tree before changing code.
3. Identify ABI, structure packing, register layout and ownership assumptions.
4. Write the smallest architecture that can support the feature correctly.
5. Define data structures and invariants.
6. Define public APIs and error semantics.
7. Define locking and interrupt-context rules.
8. Implement the real path.
9. Add positive and negative tests.
10. Build with warnings as errors.
11. Exercise the real boot/integration path.
12. Exercise physical hardware when applicable.
13. Run historical regressions.
14. Review security boundaries.
15. Measure performance where meaningful.
16. Document evidence and remaining limitations.
17. Close only the applicable checkpoint IDs.

Never skip directly from code to “done”.

## 2. Repository-first development

Before editing:

- inspect current files;
- inspect recent commits;
- find existing APIs before adding duplicates;
- preserve established naming where it is correct;
- document intentional architecture changes;
- never replace working hardware code with a mock merely to simplify tests.

Preferred implementation order inside a subsystem:

`header/types → invariants → low-level primitives → state machine → public API → integration → tests → diagnostics`.

## 3. Kernel coding model

Kernel code is freestanding C11/C17 plus minimal x86_64 assembly.

Do not use:

- glibc;
- musl;
- POSIX libc;
- Linux kernel APIs;
- hidden host services.

Every hardware register access must use correctly sized volatile operations and documented ordering requirements.

Every pointer crossing a trust boundary must have an explicit validation strategy.

## 4. Ownership and lifetime

Every major object must answer:

- Who allocates it?
- Who owns it?
- Who may reference it?
- How is lifetime extended?
- How is it destroyed?
- Can destruction race with I/O or interrupts?
- Is reference counting required?
- Which CPU may access it?

Use explicit states such as:

`NEW → INITIALIZING → ONLINE → QUIESCING → OFFLINE → DESTROYED`.

Do not free an object while an interrupt, DMA engine, worker or userspace reference can still reach it.

## 5. Locking model

Each lock documents:

- lock rank/order;
- whether interrupts may be disabled;
- whether acquisition may sleep;
- whether it is legal in interrupt context;
- ownership transfer rules.

Prevent:

- lock inversion;
- sleeping while holding a spinlock;
- freeing locked objects;
- double unlock;
- recursive acquisition unless explicitly supported.

## 6. Interrupt and deferred work

Interrupt handlers should do the minimum required to acknowledge hardware and preserve event state.

Long operations belong in deferred work/kernel threads where possible.

Every IRQ path must define:

`acknowledge → capture status → record event → EOI → deferred processing`.

Never perform unbounded filesystem/network/device work directly in a hard IRQ unless the architecture explicitly requires it.

## 7. DMA rules

DMA buffers need:

- physical-address constraints;
- alignment;
- ownership state;
- cache-coherency rules;
- mapping/unmapping lifetime;
- device visibility boundaries.

Never hand a device a pointer merely because it is valid in the CPU virtual address space.

## 8. User/kernel boundary

For every syscall:

1. validate syscall number;
2. validate argument representation;
3. validate canonical/user address range;
4. validate readable/writable permissions;
5. copy user data into kernel-owned memory;
6. perform operation;
7. copy results back only after validating destination;
8. return documented error/status.

Do not dereference user pointers directly from privileged code.

## 9. State machines

Hardware drivers must be implemented as explicit state machines rather than a sequence of optimistic register writes.

Example:

`RESET → DISCOVER → CONFIGURE → QUEUE_CREATE → ONLINE → QUIESCE → RESET/RECOVER → ONLINE`.

Every transition needs:

- entry conditions;
- register operations;
- timeout;
- failure result;
- cleanup;
- recovery path.

## 10. Filesystem implementation method

Define the on-disk format before writing the mount code.

Specify:

- endian rules;
- block size;
- version;
- checksums;
- superblock fields;
- inode layout;
- directory format;
- allocation metadata;
- journal format;
- recovery states.

Mount sequence:

`read superblock → validate bounds/version/checksum → validate geometry → validate root → recover journal if needed → construct in-memory objects → ONLINE`.

Unknown or corrupt media must fail safely. Never auto-format during mount.

## 11. Storage testing

Use disposable images for destructive tests.

Test separately:

- aligned read;
- unaligned request handling;
- multi-block I/O;
- short transfer;
- timeout;
- device reset;
- write failure;
- flush failure;
- interrupted write;
- corrupted metadata;
- full disk;
- allocation exhaustion.

For NVMe, Identify and namespace discovery are not equivalent to real read/write/flush.

## 12. USB/xHCI implementation method

Implement in this order:

1. capability registers;
2. operational registers;
3. runtime/interrupter registers;
4. DCBAA/scratchpads;
5. command ring;
6. event ring/ERST;
7. port state;
8. slot allocation;
9. device context;
10. Address Device;
11. Configure Endpoint;
12. control transfer;
13. interrupt transfer;
14. descriptor parsing;
15. HID.

Log enough TRB/context state to reproduce completion failures.

Historical completion code 11 must remain a dedicated regression scenario.

## 13. Network implementation method

Implement from the bottom up:

`DMA buffers → Ethernet → ARP → IPv4 → ICMP → UDP → TCP → sockets → DNS/DHCP → utilities`.

The network self-test must call the same driver and socket path as ordinary applications.

No fake packet generator may be used as evidence for hardware networking.

## 14. Process/ELF implementation method

Create a process only after address-space and context structures exist.

`ELF validate → create address space → map segments → create stack → build argc/argv/envp/auxv → create thread → enter ring 3`.

Every executable mapping must respect W^X policy.

Malformed ELF must be rejected without corrupting the process address space.

## 15. Shell implementation method

Do not parse and execute in one function.

Use:

`source → lexer → tokens → parser/AST → expansion → execution plan → process/pipe setup → exec → job control`.

This allows quoting, pipelines, substitutions and job control to evolve independently.

## 16. libc/musl integration

Keep the boundary explicit:

`kernel syscall ABI → startup/runtime → libc syscall wrappers → POSIX layer → applications`.

Do not change the kernel to satisfy a single libc implementation if a compatibility layer is the correct solution.

Maintain a syscall/errno compatibility table and test each supported API.

## 17. Testing pyramid

Use several layers:

### L0 — compile/static checks
Warnings, format, symbol and ABI checks.

### L1 — deterministic unit tests
Parsers, allocators, checksums, state machines.

### L2 — subsystem tests
Filesystem, scheduler, syscall, protocol tests.

### L3 — QEMU integration
Real boot and device paths.

### L4 — physical hardware
Actual target hardware.

### L5 — soak/recovery
Long runs, repeated resets, allocation pressure, I/O stress.

A higher layer never excuses failure at a lower layer.

## 18. Negative testing

Every parser and boundary needs malformed cases.

Examples:

- truncated structure;
- invalid version;
- impossible length;
- integer overflow;
- unaligned address;
- invalid pointer;
- permission violation;
- timeout;
- duplicate object;
- resource exhaustion;
- device disappearing;
- corrupted checksum;
- stale handle.

## 19. Fault injection

Build controllable failure points for development builds:

- allocator failure;
- DMA mapping failure;
- queue allocation failure;
- I/O timeout;
- completion error;
- packet drop;
- link down;
- USB disconnect;
- filesystem corruption;
- service crash.

Fault injection must produce observable failure and recovery, not a hidden success.

## 20. Observability

Every subsystem needs structured diagnostics.

At minimum record:

`timestamp + CPU + subsystem + severity + event + object/device identifier + state + error code`.

For hardware failures preserve raw status/register information where safe.

Never replace the original error with a generic “failed”.

## 21. Security review checklist

Review:

- privilege transitions;
- user pointers;
- integer overflow;
- buffer bounds;
- executable mappings;
- DMA isolation;
- device MMIO bounds;
- reference lifetime;
- credentials;
- filesystem permissions;
- IPC authorization;
- package signatures;
- installer destruction;
- secret/password storage;
- parser fuzzing.

## 22. Performance methodology

Measure a baseline first.

Use repeatable workloads and report:

- operation;
- environment;
- sample count;
- median/percentiles where useful;
- throughput;
- CPU cost;
- memory cost.

Do not optimize speculative bottlenecks.

## 23. Historical regression policy

Permanent regressions include:

- UEFI `GetMemoryMap()` invalid-opcode crash;
- NVMe controller-ready/Identify/namespace behavior;
- separate real NVMe read/write/flush;
- xHCI Address Device completion code 11;
- USB keyboard timeout followed by real `0x74` input;
- QEMU GPU `1B36:0100` vs RX 6800 XT `1002:73BF` separation;
- filesystem bad magic/version must not format;
- manual ping vs network self-test must share the same real path;
- compiler warnings such as discarded const and out-of-bounds access must not be ignored.

## 24. Checkpoint closure template

For every checkpoint create an evidence entry:

```text
Checkpoint: Pxx-yy
State: PASS | FAIL | BLOCKED | NOT TESTED | UNSUPPORTED | DEGRADED
Commit:
Environment:
Test command:
Expected observation:
Actual observation:
Evidence artifact:
Known limitations:
Reviewer:
Date:
```

`PASS` without evidence is invalid.

## 25. Definition of release-ready

A release requires:

- reproducible build;
- boot artifact;
- symbol/debug artifact;
- checkpoint ledger;
- regression results;
- hardware matrix;
- known limitations;
- recovery procedure;
- upgrade/rollback procedure;
- security review;
- documentation.

The terminal/recovery environment must remain usable even when higher-level services fail.
