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

This does not imply completion of the whole phase. ACLs, login/session lifecycle, capability interfaces, audit identity, complete metadata preservation for copy/move, broader filesystem permission matrices, and physical-hardware security evidence remain open. The shell prompt remains a presentation concern and is intentionally deferred.
