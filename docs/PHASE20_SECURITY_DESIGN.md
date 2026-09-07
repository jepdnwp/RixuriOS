# Phase 20 — Users, Groups, Credentials and Security Model

## Status

Phase 20 is **IN PROGRESS / DESIGN BASELINE RECORDED**. Phase 19 remains validated and unchanged as the functional baseline. The shell prompt redesign is explicitly deferred until Phase 20 is complete.

## Current baseline

`rix_process_t` already contains scalar `uid` and `gid` fields, and RixFS inode records contain owner UID, owner GID, and Unix-style mode bits. These fields are currently mostly descriptive: process creation defaults to UID/GID zero, filesystem creation supplies root ownership, and the syscall/VFS path does not yet enforce owner/group/other permission classes consistently. `id` and `whoami` therefore report the current root-only model honestly; they are not evidence of a completed multi-user security model.

## Phase 20 contracts

The implementation must introduce a single credential object or equivalent immutable credential fields containing real UID, effective UID, saved UID, real GID, effective GID, saved GID, and bounded supplementary groups. Fork must inherit credentials; exec must preserve credentials except for an explicitly implemented and tested set-id policy. Kernel permission checks must use the effective credentials and must never trust user-provided UID/GID values.

The first implementation slice will add versioned credential query and controlled transition syscalls, centralized access checks for read/write/execute and directory mutation, and QEMU tests for owner, group, other, and root behavior. Unsupported transitions must fail closed with a documented errno/result. No setuid/setgid behavior will be implied until file loading, inode mode handling, and rollback semantics are tested together.

The filesystem layer must preserve owner and mode metadata through create, mkdir, link, copy, move, stat, and reload. Directory search permission, parent-directory mutation permission, file read/write permission, and execute permission must be checked in one kernel-owned helper rather than reimplemented by utilities. Root bypass behavior must be explicit and narrowly scoped.

Environment handling is part of the security boundary. Child environments must be bounded, inherited intentionally, and sanitized for privileged transitions. Shell variables and `export`/`unset` semantics are separate from the Phase 19 utility baseline and must be tested before the post-Phase-20 prompt redesign.

## Required evidence gates

Phase 20 cannot be marked complete until strict build, host regressions, and real-QEMU tests demonstrate credential inheritance across fork/exec, rejected unauthorized access, allowed owner/group/other access, root policy, metadata persistence, failed transition rollback, and no privilege escalation through relative paths, redirection, pipes, or background jobs. Tests must record exact exit status and distinguish permission denial from missing-path errors.

## Deferred shell UX

After Phase 20 is complete, the shell prompt may change from `rixuri$` to a colored form such as `username@computer-name ~(directory) :`, using the actual credential username, machine name, and cwd. Prompt rendering must remain a presentation layer and must not become the source of truth for identity or permissions.


## Current implementation boundary — 2026-09-07

The credential, supplementary-group, saved-ID, chmod and set-id exec slices are implemented and QEMU-validated for the documented bounded model. Permission checks now cover directory search, file read/write, directory mutation and path-based exec authorization through the centralized VFS helper. Owner, group and other selection uses effective credentials plus inherited supplementary groups; root bypass is explicit for effective UID zero.

The current syscall surface preserves permission denial as `-EACCES` (`13`) for the covered VFS operations, while missing-path and unsupported-operation failures remain represented by the existing bounded `-EINVAL` mapping. The regression image creates fixtures with distinct owner/group/other classes and checks persisted UID/GID/mode metadata rather than relying on shell output. Setuid/setgid execution changes effective and saved credentials only after successful image replacement, and privileged execution receives an empty environment; the set-id target rejects a non-empty environment.

This does not imply completion of the whole phase. Login/session lifecycle, capability interfaces, audit identity, broader filesystem permission matrices, and physical-hardware security evidence remain open. The bounded `chown`, metadata-preserving `cp`/`mv`, and same-directory regular-file `rename` slices are now QEMU-validated; cross-directory, overwrite and multi-object transactional rename semantics remain outside this slice. The shell prompt remains a presentation concern and is intentionally deferred.


## Remaining work design — bounded v1 slices

The remaining work is split into bounded interfaces rather than an untestable claim of full POSIX security. ACL v1 keeps the existing 128-byte inode slot and adds one optional named-user entry plus one optional named-group entry, both constrained by a seven-bit permission mask. Owner mode remains authoritative for the owning UID; named-user and group entries are evaluated through a documented mask; other mode remains the fallback. ACL metadata is persistent, journaled with the inode, and exposed through versioned `get`, `set` and `clear` operations. Only the owner or a capability-authorized privileged process may mutate it.

Session v1 will make process session membership and controlling-terminal ownership enforceable instead of exposing raw setters. `setsid` creates a new session only for a process-group leader that is not already a group leader, `getsid` reports the session, and a controlling-terminal attach/detach operation requires a session leader and rejects conflicting ownership. Logout clears the controlling terminal, terminates the session’s foreground process group through the existing signal path, and leaves the kernel/recovery console available. A full interactive password database is not implied until password hashing and account-store rollback are separately validated.

Capability v1 will use a fixed, versioned capability bitmap stored out-of-line per PID so `rix_process_t` layout remains stable. The initial bits cover DAC override, owner override, setuid, setgid, TTY/session administration, audit-identity administration and non-escalating capability delegation. Effective capabilities are inherited across fork, preserved across ordinary exec, reduced on privileged exec unless explicitly retained, and can only be removed by the owning process; acquisition is restricted to the initial trusted process or an explicitly authorized capability transfer path. Root bypasses are routed through the capability check so dropping a capability has observable effect.


## Capability and session v1 evidence

The implemented capability bitmap contains DAC override, SETUID, SETGID, KILL, TTY administration, ACL administration, session administration, audit administration and delegation bits. `get_capabilities` exposes the current set and `drop_capabilities` only reduces the calling process’s set. Fork/spawn inheritance is preserved, while set-ID identity transitions clear capabilities. VFS root permission bypass requires `CAP_DAC_OVERRIDE`, root ACL mutation requires `CAP_ACL_ADMIN`, controlling-terminal operations require `CAP_TTY_ADMIN`, session login/logout requires `CAP_SESSION_ADMIN`, and cross-UID signal delivery requires `CAP_KILL`.

Session v1 now exposes session creation/query, controlling-terminal attach/detach, login and logout. A bounded in-memory session registry additionally exposes capacity-checked snapshots of live session ID, leader, UID and controlling-TTY state, and removes a record only after its last live member leaves. The QEMU lifecycle evidence validates isolated session leaders, concurrent registry visibility and logout/exit cleanup. A persistent account database, password verification service, capability delegation across ordinary exec and hardware-backed security qualification remain outside this bounded kernel slice. The dedicated cross-UID `CAP_KILL` fixture is now QEMU-validated.

## Audit identity v1 evidence

Each process has a kernel-owned audit UID in an out-of-line PID-indexed table, initialized from the creator identity and inherited by fork/spawn. Ordinary `execve` preserves the audit UID, including when the effective UID changes through a set-ID executable. The query operation copies the value through the validated user-memory path. Assignment is restricted to `CAP_AUDIT_ADMIN`; dropping that capability makes later assignment fail with `-EACCES`, and set-ID transitions clear the capability without changing the recorded audit UID.

The Phase 20 credential regression sets audit UID 4242 as the trusted root process, verifies the value remains visible after dropping the audit capability and UID/GID privileges, then forks and executes `/usr/bin/auditcheck`. The checker reports `audit=PASS` only when the inherited value survives the ordinary exec path. The disposable QEMU run passed this marker together with the existing ACL, capability, owner/group/other matrix, set-ID, CAP_KILL, session and authentication evidence without page fault, CPU exception, panic, timeout or prompt loss.

Capability delegation v1 is limited to a live direct child. The caller must hold `CAP_DELEGATE` and the delegated bits, the target must not already hold them, and `CAP_DELEGATE` itself cannot be transferred. The caller loses the transferred bits atomically; ordinary exec preserves the child’s delegated bits, while set-ID exec clears all effective capabilities. Zombie, unrelated and invalid targets fail closed.

The continuation evidence gate requires `cap=PASS`, `acl=PASS`, `matrix=PASS`, `audit=PASS`, `delegated-exec=PASS`, `delegation=PASS`, `setid=PASS`, `kill=PASS` and `session=PASS`, with no page fault, CPU exception or kernel panic markers. The shell prompt remains intentionally deferred until the broader Phase 20 security boundary is complete.
