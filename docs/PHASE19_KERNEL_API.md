# Phase 19 Kernel API Design for System Utilities

## Scope

This document defines the next ABI needed by `df`, `free`, `dmesg`, `mount` and `umount`. It is a design boundary, not an implementation claim. Until the kernel paths exist and are exercised through QEMU, the utilities must not print synthetic values or return success.

## Proposed syscall identifiers

The identifiers below are reserved for the RixuriOS ABI and must be added only together with version checks and user-pointer validation.

| Call | ID | Purpose |
|---|---:|---|
| `statfs` | 137 | Return filesystem capacity and mount identity for a path. |
| `sysinfo` | 138 | Return physical-memory/page accounting and uptime snapshot. |
| `klog_read` | 139 | Read a cursor-based kernel log ring with loss reporting. |
| `mount` | 165 | Attach a validated block/filesystem source into a namespace. |
| `umount` | 166 | Detach a mount after reference and busy checks. |

The numbers are provisional until recorded in the stable syscall-number policy. Unknown calls must continue to return `-ENOSYS`.

## `statfs`

The kernel should fill a versioned structure containing `version`, `struct_size`, filesystem type, block size, total blocks, free blocks, available blocks, inode totals, inode free count, mount ID and read-only flags. Counts must be snapshots with documented consistency, and all arithmetic must saturate or fail on overflow. `df` should distinguish `free` from `available` and return a non-zero status for an invalid path, unmounted source or malformed structure size.

## `sysinfo`

The memory snapshot should contain page size, total physical pages, free pages, reserved pages, kernel pages, user pages, cache pages, swap totals if supported, and monotonic uptime. The implementation must define whether counts are instantaneous or sampled and must never infer values from allocator internals that are not globally accounted. `free` should reject a kernel version or structure size it does not understand.

## `klog_read`

The kernel log must be a bounded ring of records containing a monotonically increasing sequence, timestamp, severity, subsystem and payload length. A reader supplies a cursor and capacity; the result reports the next cursor and whether records were lost because the reader fell behind. Access must be privilege-checked, payloads must be copied out with uaccess, and concurrent writers must not expose partially written records. `dmesg` should return non-zero on invalid cursors, permission denial or loss when strict mode is requested.

## `mount` and `umount`

Mounting requires a namespace, source descriptor or device identity, target path, filesystem type, flags and an ownership/credential context. The transaction must validate the source, mount the filesystem, publish the namespace entry only after success, and roll back every allocation on failure. Unknown filesystem versions, corrupt superblocks and unsupported flags must fail without formatting.

Unmounting must reject busy mounts, active file references and the root mount unless an explicit shutdown policy exists. It must quiesce I/O, flush or report errors according to flags, remove the namespace entry atomically, and preserve diagnostics for recovery. Both calls need idempotence rules, concurrent namespace locking, and a QEMU disposable-image test matrix covering success, invalid source, corrupt media, busy target and rollback.

## Utility mapping and evidence gates

`df` requires `statfs` positive/negative and pipeline tests. `free` requires `sysinfo` accounting consistency against allocator diagnostics. `dmesg` requires boot, overflow/loss and privilege tests. `mount`/`umount` require real disposable block-device QEMU tests and explicit no-format behavior. None of these gates can be closed by detecting a device or printing a fixed placeholder.
