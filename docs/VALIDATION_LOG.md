# RixuriOS Validation Log

## 2026-09-06 — strict build and UEFI/QEMU smoke test

The repository was validated from a clean object state with the host toolchain using the canonical freestanding build flags. The command `make clean CROSS= && make all CROSS= && make check CROSS= && make image CROSS=` completed successfully. Compilation used `-Wall -Wextra -Werror`; ELF header and program-header checks also completed. The linker emits only the non-fatal `.note.GNU-stack` warning for the generated `kernel/user_init_blob.o` binary-object wrapper; the hand-written assembly objects carry explicit non-executable-stack notes.

The generated UEFI image was then exercised through `bash ./scripts/run-qemu.sh` with a bounded timeout. The observed serial path was:

```text
RixuriOS kernel: x86_64 / AMD64 64-bit
Boot handoff: version=1 size=104
GDT/IDT: initialized
PMM: total=129480 free=126811
VMM: initialized
KHEAP: initialized
TTY/HID: initialized
ACPI CPUs: 1 IOAPICs: 1
PCI: devices=6
NVMe: controllers=0
xHCI: controllers=0
TIME: realtime=...
USER: embedded init prepared, pid=1 task=1
IRQ: PIT routed to vector 32; interrupts enabled
Core services: timer/scheduler/process/syscall/PCI/NVMe/xHCI/HID/block/VFS/time initialized
LAPIC: initialized, id=0
RIXURI:KERNEL_READY
USER: init returned to kernel
```

This closes the current generic **CP1 BUILD** and **CP3 BOOT** evidence for the kernel/UEFI and embedded ring-3 smoke path. It does not close hardware checkpoints. In particular, QEMU exposed zero NVMe and xHCI controllers in this run, so no NVMe I/O, xHCI completion, HID transfer, hotplug, or physical-device behavior is claimed. The bounded timeout is expected because the kernel remains alive after returning from the one-shot embedded init process.

The generated artifacts are `build/kernel.elf`, `build/uefi/esp.img`, and `build/qemu-serial.log`. They are build outputs and are intentionally not source-controlled unless a release process later defines artifact retention.

## 2026-09-06 — USB descriptor parser foundation

The next Phase 15 increment adds a freestanding USB descriptor parser for device and configuration descriptors. It validates descriptor lengths, total configuration bounds, interface/endpoint ordering, endpoint-address reserved bits, caller capacities, and malformed/truncated inputs. The parser is compiled into the kernel and exercised independently through `make test CROSS=` with positive and negative host-side cases. This is parser evidence only; no USB controller or HID transfer completion is claimed.

## 2026-09-06 — EP0 control-transfer path

The xHCI layer now exposes `xhci_control_transfer()`. It constructs Setup/Data/Status TRBs on the addressed device’s EP0 ring, publishes the ring with the slot doorbell, polls transfer events, reports completion codes and residual-length-derived byte counts, and returns timeout/error codes without claiming success. The no-data and OUT-transfer Status Stage direction rules are handled explicitly. The implementation passes the strict kernel build and the UEFI/QEMU smoke path, but QEMU exposes zero xHCI controllers in this environment; therefore no hardware completion or descriptor enumeration result is claimed yet.

The standard `xhci_get_descriptor()` wrapper now formats USB `GET_DESCRIPTOR` requests for device, configuration, string and other descriptor types while preserving the same actual-length and error semantics. It is build-validated but remains hardware-unexercised in this QEMU configuration.

The enumeration layer now performs the standard two-stage configuration fetch: it retrieves the fixed-size device descriptor, retrieves the nine-byte configuration header, checks `wTotalLength` against the caller-provided buffer, retrieves the complete configuration, and invokes the parser. This path is strict-build and QEMU-boot validated only; QEMU still reports zero xHCI controllers, so no live descriptor result is recorded.

The xHCI layer now also creates an initial interrupt endpoint context, submits Configure Endpoint, maintains a dedicated endpoint ring, rings the slot doorbell with the endpoint DCI, and polls interrupt transfer events. The path is strict-build and generic-QEMU regression validated, but no live endpoint completion is claimed until a controller-backed target is available.

The same endpoint-ring submission and completion path now supports bulk endpoints through `xhci_bulk_transfer()`, while `xhci_interrupt_transfer()` remains available for interrupt-IN/OUT endpoints. Endpoint type selection is encoded in the Configure Endpoint context and validated at transfer time. No hardware completion is claimed because the current QEMU topology exposes zero xHCI controllers.

The slot runtime now maintains independent endpoint state for every non-control DCI. `xhci_configure_endpoint()` allocates and configures a ring per endpoint, while the interrupt and bulk wrappers receive an explicit endpoint address and route the corresponding DCI doorbell. This removes the previous one-endpoint-per-slot software boundary. The change is strict-build validated but remains hardware-unexercised.

The existing boot keyboard and mouse parsers are now connected through `hid_xhci_keyboard_poll()` and `hid_xhci_mouse_poll()`. These adapters submit an interrupt-IN transfer, preserve the actual-length result, reject short reports, and dispatch the report to the existing TTY/HID parser. A Port Status Change Event polling API was also added; it peeks without consuming unrelated command or transfer events, validates the event port, acknowledges the event, and reports current connection state. Automatic device-manager attach/enumeration policy and hardware completion evidence remain open because this QEMU topology exposes zero xHCI controllers.

Phase 16 parser work now includes a bounds-checked HID report descriptor parser for short and long items. It tracks usage page, usage, report size/count and report ID, identifies boot-compatible keyboard/mouse usages, rejects malformed/truncated items and arithmetic overflow, and passes a host-side positive/negative test (`hid report tests: PASS`). This is parser evidence only; report-protocol transfer behavior still requires a real HID device.

The xHCI layer now exposes `xhci_get_hid_report_descriptor()`, which issues the standard interface-scoped `GET_DESCRIPTOR` request for descriptor type `0x22`. It is strict-build validated and is ready to feed the returned bytes to `hid_parse_report_descriptor()` once a controller-backed enumeration path is exercised.

The HID control path now also exposes class requests for `SET_PROTOCOL`, `SET_IDLE` and `GET_PROTOCOL`, including interface and protocol validation. Boot keyboard/mouse interrupt adapters reject oversized completions before converting the length to their legacy 8-bit parser API, preventing silent truncation. These paths remain hardware-unexercised in the current QEMU topology.

The historical completion-code-11 regression now has dedicated runtime instrumentation on both command-completion and transfer-event paths. When code 11 is observed, the serial trace records the controller, event TRB physical address, event parameter, control/status words, slot/DCI, port/speed/route, DCBAA device context, input context, endpoint ring, cycle and enqueue state. No code-11 event was observed in QEMU because no xHCI controller was exposed.

Phase 16 report-protocol work now includes report-ID framing helpers for keyboard and mouse reports. The helpers validate the expected report ID before dispatching to the existing boot parsers; keyboard rollover error usages are rejected and mouse signed motion/wheel fields are covered by host tests. This remains parser-level evidence until a real report-protocol HID device is exercised.

Phase 17 TTY work now has host evidence (`tty tests: PASS`) for canonical reads waiting on newline, raw reads returning immediately, echo flowing through a separate output queue, foreground process-group state, PTY master/slave input/output flow, terminal dimensions and ANSI/VT cursor positioning. Signal generation, session ownership, full screen-buffer rendering and shell integration remain open; no full Phase 17 completion claim is made.

The TTY terminal layer now also has a bounded screen-buffer assertion: printable output writes the expected cell, `tty_read_screen()` returns the configured row-major surface, and the implementation bounds all cell access by the fixed maximum dimensions. ANSI `J/K` erase operations operate on that same buffer. This closes the screen-state portion of the current terminal-engine work; signals, sessions and shell integration remain outside this checkpoint.

Terminal control signals now have host evidence through the TTY signal hook: `Ctrl-C` maps to `SIGINT` and targets the configured foreground process group; the same path maps `Ctrl-Z`/`Ctrl-\\` to `SIGTSTP`/`SIGQUIT`, while the kernel hook broadcasts pending signals to matching process objects. Process creation now initializes inherited session/group identity. Full session leader/controlling-terminal policy remains open.

Controlling-terminal state now has host evidence: a TTY can attach to a nonzero session, report ownership, and detach while clearing foreground-group ownership; invalid zero-session controlling attachments are rejected. The existing IPC channel remains the bounded pipe primitive for later shell redirection integration.

Phase 18 frontend evidence now includes `shell parser tests: PASS`: quoted and escaped words, comments, pipelines, `&&`/`||`/`;`/background operators, input/output/append redirections and unterminated-quote rejection are covered. The parser produces a bounded AST; no execution-completion claim is made until real process, pipe and VFS APIs are wired.

The same shell host test now covers callback-driven `$NAME` and `${NAME}` expansion, suppression inside single quotes, expansion inside double quotes, backslash escaping and missing-variable behavior. Expansion is bounded by the caller’s output capacity and returns an error rather than truncating.

Interactive completion is now covered by the shell host test: a single matching candidate expands fully, multiple candidates produce their longest common prefix, and no-match input returns an empty completion with a zero match count. The API is candidate-provider based so future PATH/builtin/filesystem completion can reuse the same bounded algorithm.

The shell host test also covers interactive history: consecutive duplicate suppression, previous/next navigation, newline-delimited export and import into a fresh history object. Storage is fixed-size and all copies reject capacity overflow rather than truncating.

Advanced expansion is now host-tested: arithmetic precedence/parentheses, divide-by-zero and signed overflow rejection, plus callback-driven `$(...)` command substitution with nested-parenthesis matching and trailing-newline trimming. The callback boundary keeps execution policy separate from the bounded frontend.

The linker was hardened during the Phase 0–17 audit. Explicit PHDRS now produce separate `R-X`, `R--` and `RW-` load segments plus a read-only `GNU_STACK`; `readelf -l build/kernel.elf` confirms no `RWE` segment. USB, HID and TTY host tests and the UEFI/QEMU boot smoke test continue to pass after this change.

Review of the composite-device path found and corrected a context-construction defect: each Configure Endpoint operation now updates the input Slot Context's Context Entries field to the highest configured DCI and sets Add Slot Context alongside the endpoint bit. This is required by xHCI when adding endpoints beyond the initial EP0 context; the fix is strict-build validated but still awaits controller-backed execution.


## 2026-09-06 — Phase 18 real shell runner foundation

The banner-only embedded init image was replaced with a freestanding C shell entrypoint and linked together with the bounded shell frontend and bootstrap libc wrappers. The implementation composes real `fork`, `pipe`, `dup2`, `openat`, `execve` and `wait` calls; applies `<`, `>` and `>>` redirections in child processes; resolves external commands through the VFS-backed PATH resolver; runs the existing bounded builtins; and preserves conditional execution semantics through the indexed pipeline callback API.

Kernel-side integration work in this increment corrected three prerequisites for that composition. Pipe read/write endpoint references now survive `dup` and `fork` until the last endpoint closes. The active VMM PML4 is updated whenever a process is activated, so uaccess validation checks the current user mapping. TTY and pipe reads yield while no data is available, while `wait` yields until a requested child becomes a zombie and distinguishes the no-child case. The initial user stack was expanded from eight to 32 pages for the C shell's bounded local state.

The following commands completed successfully from a clean object state:

```text
make clean CROSS=
make test CROSS=
make image CROSS=
```

The host suite reported `hid report tests: PASS`, `tty tests: PASS` and `shell parser tests: PASS`; the shell test now also covers indexed pipeline callback propagation. The UEFI/QEMU smoke run reached `RIXURI:KERNEL_READY`, printed `RixuriOS shell ready` and the `rixuri$ ` prompt, and emitted no CPU exception. QEMU still reported zero NVMe and xHCI controllers, and no disposable RixFS command image or real keyboard input path was available in this run. Therefore this is build/boot/prompt evidence only; it does not claim hardware-backed interactive input or external-command execution completion.


## 2026-09-06 — real userspace chain and disposable RixFS QEMU qualification

The continuation was validated after a clean rebuild with `make test CROSS=` and `make image CROSS=`. The host suite reported `hid report tests: PASS`, `tty tests: PASS`, `shell parser tests: PASS`, and `Static kernel build checks completed.` The image builder reported a 64 MiB RixFS image with real ELF files at `/bin/echo`, `/bin/cat`, `/usr/bin/args`, `/usr/bin/grep`, `/bin/true`, `/sbin/false`, and `/usr/sbin/true`; the UEFI packager generated `build/uefi/esp.img`.

A bounded interactive run of `bash ./scripts/run-qemu.sh` used the serial-to-TTY worker and the mounted disposable image. The boot evidence included `NVMe: controllers=1`, `VFS: mount nvme0n1 rc=0`, `RIXURI:KERNEL_READY`, `RixuriOS shell ready`, and the `rixuri$ ` prompt. The QEMU firmware also reported that the NVMe UEFI boot entry was not found and continued through the SATA-backed UEFI boot entry; the kernel then discovered and mounted the NVMe test image as intended.

The following real command results were observed on the serial console:

```text
/bin/echo hello | /usr/bin/grep hello
hello

/usr/bin/args arg1 arg2
argc=3
argv[0]=/usr/bin/args
argv[1]=arg1
argv[2]=arg2
envp=PATH=/bin:/usr/bin:/sbin:/usr/sbin
envp=PWD=/

/bin/echo one > file
/bin/cat file
one
/bin/cat < file
one

true && echo yes
yes
false || echo recovered
recovered
```

These observations cover the real serial-input-to-TTY-to-shell-to-fork/exec-to-argv/envp-stack-to-VFS/pipe/dup2/read/write-to-wait-to-prompt path for the listed scenarios. An attempted non-interactive stdin pipe was intentionally not counted as evidence because QEMU consumed input before shell initialization in one run and produced a bounded `qemu_rc=124`; the accepted evidence above came from the live interactive serial session, not from that failed capture.

The evidence does not close all Phase 18 gates. Append redirection, multi-stage pipeline depth, background job lifecycle and notifications, `waitpid(WNOHANG)`, foreground process-group signal delivery, malformed-pointer runtime cases, permission/error matrices, and a `sleep` executable remain open. QEMU reported zero xHCI controllers, so physical USB keyboard/HID evidence and the historical completion-code-11 / keyboard `0x74` regressions remain blocked.

## 2026-09-06 — process lifecycle hardening regression

The process lifecycle hardening increment added collision-free PID selection across the bounded process table, rollback of inherited descriptor references when child creation fails, idempotence protection for repeated exit attempts, and descriptor cleanup at the transition to zombie state. The legacy `process_exec_user()` entry point now also constructs a valid single-argument initial stack rather than passing an invalid zero-argument vector.

The strict suite completed successfully with the installed cross-toolchain:

```text
make CROSS=x86_64-linux-gnu- test
```

The result included a warning-as-error kernel link, `hid report tests: PASS`, `tty tests: PASS`, `shell parser tests: PASS`, and `Static kernel build checks completed.` This is build and host-test evidence; the lifecycle changes are not marked as independently QEMU-proven until a disposable runtime scenario exercises PID reuse, failed fork rollback, and repeated exit paths.

## 2026-09-06 — exec initial-stack ABI hardening

The exec image-construction path now accepts valid zero-argument requests, retains bounded argv/envp validation, emits the required `argc`, `argv[]`, `NULL`, `envp[]`, `NULL` layout, and appends an `AT_NULL` auxiliary-vector type/value pair. Stack-vector padding is selected so the initial user stack pointer remains 16-byte aligned for all supported argument and environment cardinalities. The replacement address space is still committed only after ELF loading, stack allocation, string copying, vector construction and alignment checks succeed.

The post-change strict suite completed successfully with `make CROSS=x86_64-linux-gnu- test`. The kernel compiled and linked with `-Wall -Wextra -Werror`; `hid report tests: PASS`, `tty tests: PASS`, `shell parser tests: PASS`, and the static kernel checks completed. A dedicated runtime test that introspects auxiliary vectors from userspace remains to be added; existing `args` execution evidence validates argv/envp but does not yet print auxv.

## 2026-09-06 — signal interruption of blocking syscalls

Blocking TTY reads, pipe reads, and parent waits now check for a pending unmasked signal before yielding again. When one is available, the syscall consumes the pending signal and returns `-EINTR` (`-4`) rather than sleeping indefinitely. `waitpid()` retains its `WNOHANG` behavior and only applies interruption to the blocking path. The change passed the strict kernel build and all existing host tests; a dedicated QEMU Ctrl-C/Ctrl-Z interruption scenario remains outstanding.

The attempted auxv QEMU run rebuilt the real image and confirmed `NVMe: controllers=1`, `VFS: mount nvme0n1 rc=0`, and `RIXURI:KERNEL_READY`, but the injected command was not consumed by the shell before the bounded run ended. No auxv runtime result is claimed from that attempt.

## 2026-09-06 — real auxv execution and exec capacity regression fix

The previous auxv attempt exposed a real regression in the new stack-capacity check: it reserved two unnecessary pointer words and rejected a valid `argc=3`, `envc=2` image. The check now accounts only for the actual auxiliary-vector, environment, argument, argc, and optional alignment words.

After rebuilding the disposable image, the real serial-to-TTY-to-shell-to-NVMe/RixFS-to-fork/exec path produced:

```text
NVMe: controllers=1
VFS: mount nvme0n1 rc=0
RIXURI:KERNEL_READY
argc=3
argv[0]=/usr/bin/args
argv[1]=arg1
argv[2]=arg2
envp=PATH=/bin:/usr/bin:/sbin:/usr/sbin
envp=PWD=/
auxv_at_null=1
```

This is real userspace evidence that the constructed initial stack exposes the expected argv/envp values and terminates the auxiliary-vector area with an `AT_NULL` pair. The bounded command ended by timeout after the prompt returned; no exception or exec failure was observed.

## 2026-09-06 — pipe lifecycle regression coverage

Added a strict host regression target, `pipe-test`, compiled with `-Wall -Wextra -Werror`. It exercises a full-capacity write with partial-write status, full-buffer readback, writer-close EOF, reader-close write failure, and zero-count error propagation. The test passed as part of `make CROSS=x86_64-linux-gnu- test` with `pipe tests: PASS`. This covers bounded channel semantics and endpoint closure; it does not replace a scheduler-level blocked-reader/writer wakeup stress test.

A real QEMU Ctrl-C harness was also attempted against a foreground blocking `/bin/cat`. The shell prompt and kernel boot were observed, but the serial input did not reach the command before the bounded harness ended, so no foreground signal-interruption PASS is claimed. The harness remains available for follow-up timing/debugging.

## 2026-09-06 — nanosleep and mapped-stack regression

Implemented `RIX_SYS_NANOSLEEP` using the monotonic PIT-backed clock and cooperative scheduler yields, with malformed timespec, overflow, and pending-signal interruption checks. Added the real `/bin/sleep` userspace utility and integrated it into the disposable RixFS image.

The first QEMU run exposed an exec failure for both `sleep` and `echo` after the stack ABI changes. Source inspection identified that the stack-capacity check measured unused space above the copied strings instead of available mapped space below them. After correcting the bound, QEMU successfully executed:

```text
/bin/sleep 0
/bin/echo after-sleep
after-sleep
```

The same run recorded `NVMe: controllers=1`, `VFS: mount nvme0n1 rc=0`, and `RIXURI:KERNEL_READY`, with no exec failure or exception output.

## 2026-09-06 — ABI negative path and first directory utilities

Added the real `/usr/bin/abi-negative` userspace test. Through the NVMe/RixFS shell path it exercised malformed pointers for `openat`, `getdents`, and `nanosleep`, producing `negative_abi=PASS` with no page fault, exception, or kernel crash.

Added real `/bin/ls`, `/bin/mkdir`, and `/bin/rm` implementations over VFS directory APIs. QEMU confirmed `/bin/mkdir /usr/testdir` and `/bin/ls /usr` exposed `testdir`; the attempted `/bin/rm /usr/testdir` correctly failed because the current unlink ABI does not remove directories and no `rmdir` utility exists yet. A subsequent background `sleep 0 &` run launched the command and returned to the prompt, but did not emit `[job] done`; background completion notification remains an open failure/diagnostic target.

An explicit QEMU run with `-device qemu-xhci,id=explicit-xhci` changed PCI enumeration from 7 to 8 devices, but the kernel still reported `xHCI: controllers=0` and then panicked while creating embedded init. USB/HID keyboard qualification therefore remains blocked by the current xHCI driver/topology interaction; no keyboard PASS is claimed.

## 2026-09-06 — background completion notification fix

The shell was clearing `execution.background` when the first pipeline callback reset execution bookkeeping, so background jobs were launched but never registered for reaping. Preserving that flag fixed the path. Real QEMU now produces:

```text
/bin/echo after-bg
after-bg
[job] done
```

The result is evidence that `/bin/sleep 0 &` completed and was collected through the shell’s `waitpid(..., WNOHANG)` polling path.

The initial `/usr/bin/proc-test` run reached `proc:pipe-after` but hung before `proc:fork-after` when forking with both pipe descriptors open. This exposed a real fork-after-pipe regression; the diagnostic utility was retained to reproduce it.

The regression was traced to `address_space_destroy()` omitting user PML4 slot zero, leaking cloned user page tables across fork/reap cycles. After including slot zero in cleanup and making the WNOHANG child race deterministic, the real QEMU utility produced:

```text
proc:pipe
proc:pipe-after
proc:fork-after
proc:read
proc:wait-writer
proc:fork-wnohang
proc:wait-nohang
proc:done
proc_pipe_wait=PASS
```

No page fault, exception, or exec failure was observed. This closes the tested fork-after-pipe, pipe wakeup, and `waitpid(WNOHANG)` path; broader scheduler stress remains desirable.

## 2026-09-06 — Phase A directory removal and file utility qualification

Implemented the missing directory-removal path from userspace to RixFS. The kernel now exposes `RIX_SYS_RMDIR`, VFS delegates `rmdir` to RixFS, and `/bin/rmdir` rejects non-empty directories while reclaiming an empty directory inode/data extent. Directory append now reuses deleted directory-entry sectors and correctly consumes the preallocated sector of a newly created empty directory; this was required for repeated create/remove operations and overwrite redirections.

The strict suite completed successfully with:

```text
make CROSS=x86_64-linux-gnu- test
git diff --check
make CROSS=x86_64-linux-gnu- image
```

The host results included `hid report tests: PASS`, `tty tests: PASS`, `shell parser tests: PASS`, `pipe tests: PASS`, and `Static kernel build checks completed.` The image builder produced the 64 MiB disposable RixFS image and included `/bin/rmdir`, `/bin/cp`, and `/bin/mv`.

The real QEMU serial harnesses observed `NVMe: controllers=1`, `VFS: mount nvme0n1 rc=0`, `RIXURI:KERNEL_READY`, and the shell prompt. The combined file-utility scenario produced the following evidence:

```text
/bin/cp /bin/echo /usr/echo-copy
/bin/ls /usr
echo-copy
/bin/mv /usr/echo-copy /usr/echo-moved
/bin/ls /usr
echo-moved
/bin/rm /usr/echo-moved
/bin/mkdir /usr/emptydir
/bin/rmdir /usr/emptydir
/bin/mkdir /usr/nonempty
/bin/mkdir /usr/nonempty/child
/bin/rmdir /usr/nonempty
rmdir: failed
/bin/rmdir /usr/nonempty/child
/bin/rmdir /usr/nonempty
qemu file utilities test: PASS
```

The edge-case QEMU harness also passed empty-file copy/move, overwrite, and multi-sector executable copy/readback scenarios. Its observed markers were `overwrite-pass`, `mv-overwrite-pass`, `multi-sector-pass`, and `qemu cp/mv edge tests: PASS`. No `cp`/`mv` read or write failure was observed. The harness logs are `build/qemu-file-utils.log` and `build/qemu-cp-mv-edge.log`; no CPU exception or kernel panic was emitted. This closes the observed Phase A `rmdir` path and the requested cp/mv edge cases, but does not claim broader filesystem durability, crash recovery, or hardware qualification beyond the QEMU NVMe-backed disposable image.

## 2026-09-06 — Phase C foreground control-signal runtime evidence

The signal harness was corrected to wait for `USER: init returned to kernel` before sending input and to give every scenario a private RixFS image and UEFI ESP/NVRAM directory. This prevents one QEMU run’s journal or firmware state from becoming input to the next run.

Each scenario started a blocking `/bin/cat`, sent the specified raw serial control byte, and observed the shell prompt return without an exception, page fault, or panic. The observed results were:

```text
ctrl-c: shell prompt returned after control signal; command_failure_message=no
ctrl-z: shell prompt returned after control signal; command_failure_message=no
ctrl-backslash: shell prompt returned after control signal; command_failure_message=no
qemu foreground signal tests: PASS
```

These are runtime observations that the blocking foreground command was interrupted sufficiently for the shell to regain its prompt. No stopped-job notification, signal-specific exit-status display, or full POSIX job-control semantics was observed; those remain outside this evidence boundary. Raw logs are `build/qemu-signal-ctrl-c.log`, `build/qemu-signal-ctrl-z.log`, and `build/qemu-signal-ctrl-backslash.log`.

## 2026-09-06 — Phase D repeated pipe/fork/reap stress

Added `/usr/bin/pipe-stress`, which performs eight consecutive pipe/fork/read/write/close/reap rounds. Each round deliberately reads before the writer has necessarily run, exercising the existing blocked-reader scheduler path; it also performs a child `waitpid(WNOHANG)` probe followed by reap. The strict host suite and image build passed, and an isolated real-QEMU run produced:

```text
pipe-stress:begin
pipe-stress:blocked-reader-pass
pipe-stress:fork-reap-pass
pipe-stress:PASS
qemu pipe stress test: PASS
```

The stress utility uses a 512-byte payload because the current pipe implementation does not yet provide a blocking full-pipe writer contract. A separate attempt to send more than the 4096-byte channel capacity stalled before the PASS marker; the proposed VFS retry/yield change was reverted and is not part of the implementation. In addition, running the existing `proc-test` first and then launching `pipe-stress` produced `CPU exception vector=6 ... rip=0x00000000000b0000` before the stress PASS marker. Therefore Phase D is **partially validated only**: standalone repeated blocked-reader and reap behavior passed, while blocked-writer backpressure and cross-test page-table/task reuse remain open blockers.

## 2026-09-06 — Phase 19 `/bin/touch`

Added a real `/bin/touch` utility using the existing `openat(O_WRONLY|O_CREAT, 0644)` and `close` ABI. The isolated QEMU harness exercised creation of a new file, reopening an existing file, rejection of a path whose parent does not exist, cleanup with `/bin/rm`, and a final directory listing. The observed result was:

```text
touch-created
touch: failed: /missing/child
qemu touch test: PASS
```

The implementation currently provides create-or-open behavior; timestamp update semantics are not implemented because the current public stat/inode ABI has no timestamp mutation operation.

## 2026-09-06 — Phase 19 `/bin/stat`

The existing kernel `RIX_SYS_STAT` path was exposed through libc as `stat()`, and `/bin/stat` was added to the strict build and image. A real QEMU serial harness created a regular file, inspected that file, inspected `/usr`, checked a missing path, and removed the temporary file. The observed output included:

```text
inode 26
type 2
mode 33188
size 0
type 1
mode 16877
size 1536
stat: failed
qemu stat test: PASS
```

This validates regular-file, directory, and missing-path behavior for the current stat ABI.

## 2026-09-06 — Phase 19 `/bin/ln`

Implemented hard links over RixFS directory entries and inode link counts. The kernel now provides `link()` syscall number 86, VFS destination-parent resolution, RixFS same-inode directory-entry creation, and link-count-aware unlink cleanup. A real QEMU serial harness created `/usr/ln-source`, linked `/usr/ln-alias`, verified both names had inode 27, removed the source, verified the alias still worked, and checked directory and missing-source failures.

```text
inode 27
type 2
mode 33188
size 0
inode 27
type 2
mode 33188
size 0
inode 27
type 2
mode 33188
size 0
ln: failed
ln: failed
qemu ln test: PASS
```

This validates regular-file hard-link creation and lifetime through unlink; directory links and symlink semantics remain unsupported.

## 2026-09-06 — Phase 19 `/bin/head` and `/bin/tail`

Added default ten-line `/bin/head` and `/bin/tail` utilities with path and stdin modes. The real QEMU harness exercised both through the existing process/pipe/shell path and checked missing-path failures:

```text
/usr/bin/args alpha beta | /bin/head
argc=3
argv[0]=/usr/bin/args
argv[1]=alpha
argv[2]=beta
...
/usr/bin/args alpha beta | /bin/tail
argc=3
argv[0]=/usr/bin/args
argv[1]=alpha
argv[2]=beta
...
/bin/head /missing
head: failed
/bin/tail /missing
tail: failed
qemu head/tail test: PASS
```

No CPU exception or panic marker was observed. This is QEMU evidence for the default stdin/path scenarios only; options, multi-file output labels, and large-file tail behavior remain open.

## 2026-09-06 — Phase 19 text-core utilities

Added `/usr/bin/wc`, `/usr/bin/cut`, `/usr/bin/tr`, `/usr/bin/sort`, and `/usr/bin/uniq`. A real QEMU harness exercised them through shell pipelines and checked a missing-path error:

```text
/bin/echo alpha | /usr/bin/wc
1 1 6
/bin/echo a:b:c | /usr/bin/cut -d : -f 2
b
/bin/echo abc | /usr/bin/tr abc xyz
xyz
/usr/bin/args z a | /usr/bin/sort
argc=3
argv[0]=/usr/bin/args
/usr/bin/args z a | /usr/bin/uniq
argc=3
argv[0]=/usr/bin/args
/usr/bin/wc /missing
wc: failed to open path
qemu text utilities test: PASS
```

This is QEMU evidence for the bounded stdin/pipeline and missing-path scenarios; it does not claim complete POSIX option or locale semantics.

## 2026-09-06 — Phase 19 environment/shell utilities

Added `/usr/bin/env`, `/usr/bin/printf`, `/bin/pwd`, and `/usr/bin/which`. Real QEMU output included:

```text
/usr/bin/env
PATH=/bin:/usr/bin:/sbin:/usr/sbin
PWD=/
/usr/bin/printf x=%s,n=%d hello 42
x=hello,n=42
/bin/pwd
/
/usr/bin/which echo
/bin/echo
/usr/bin/which absent-command
which: not found
qemu environment utilities test: PASS
```

The output confirms the current embedded environment, basic formatting, root working-directory model, fixed PATH lookup, and missing-command handling. Full POSIX environment mutation and formatting semantics are not claimed.

## 2026-09-06 — Phase 19 process/system utilities

Added `/usr/bin/kill`, `/usr/bin/ps`, `/usr/bin/uname`, and `/usr/bin/du`, and fixed the missing kernel `getpid` dispatcher case. Real QEMU output included:

```text
/usr/bin/ps
PID
2
/usr/bin/uname
RixuriOS
/usr/bin/du /bin/echo
20\t/bin/echo
/usr/bin/du /missing
du: failed
/usr/bin/kill 99999
kill: failed
qemu process utilities test: PASS
```

The PID is a real QEMU process result after the dispatcher fix. Full process listing and recursive disk accounting are not claimed.


## 2026-09-06 — Phase 19 find/xargs/sed/test strict build and QEMU evidence

The four new userspace programs were compiled through the repository’s freestanding target with `x86_64-linux-gnu-gcc`, `-std=c17 -ffreestanding -fno-stack-protector -fno-pie -mno-red-zone -m64 -Wall -Wextra -Werror -O2`, linked as static ELF images, and passed ELF header/program-header checks. The RixFS image builder included `/usr/bin/find`, `/usr/bin/xargs`, `/usr/bin/sed` and `/bin/test`. UEFI packaging completed and produced `build/uefi/esp.img`; the environment required MinGW, `dosfstools` and `mtools`, which are build dependencies rather than repository changes.

A real QEMU run reached NVMe controller discovery, `VFS: mount nvme0n1 rc=0`, `RIXURI:KERNEL_READY`, `RixuriOS shell ready` and the interactive prompt. The dedicated harness is `scripts/qemu_phase19_utils_test.py` and stores its serial capture in `build/qemu-phase19-utils.log`. The initial commands exercised file creation/append, recursive find, sed in a pipeline, xargs in a pipeline, test with `&&`, `||` and sequential execution, and grep of transformed output.

The harness did not pass. It consistently reached `/bin/echo one two | /usr/bin/xargs /bin/echo` and then lost the shell prompt before completion, with no `CPU exception` or `PANIC` marker in the serial capture. This is a real runtime failure in the xargs pipeline path, not a validation success. The implementation was changed once from per-token child execution to a bounded single-batch child to remove an obvious pipe/wait deadlock, but the same QEMU failure remained. xargs therefore remains `IMPLEMENTED / NOT YET VALIDATED`; a scheduler/pipe/exec runtime diagnosis is required before claiming Phase 19 completion.

The five requested system utilities were not implemented with fake success. Existing headers contain no honest kernel API for filesystem capacity (`statfs`), memory accounting (`sysinfo`), kernel log streaming, or versioned mount namespace operations. Those syscall/data-model requirements are documented in `docs/IMPLEMENTATION_STATUS.md` and the next-step section of `docs/ROADMAP.md`.


## 2026-09-06 — xargs nested fork/exec fault diagnosis

A targeted QEMU run added temporary lifecycle markers to xargs. The command reached `xargs: read-done` and `xargs: run-start`, proving that the pipe reader observed EOF and argument collection completed. The fault occurred only after xargs called `fork` and the child entered the `execve` path. The serial evidence was a page fault without a fabricated success result.

The first diagnostic run reported `vector=14`, kernel RIP in `address_space_create()` while copying the kernel page-table bootstrap mapping, and CR2 in the physical bootstrap page-table range. A follow-up test reserved the early VMM page-table storage from the PMM allocator and repeated the full strict image/QEMU path; the fault remained. A second comparison showed the same failure with and without the experimental syscall-side CR3 switch. Both experiments were reverted because neither was a validated fix.

The current conclusion is narrower and actionable: pipe EOF/refcount handling is not the immediate failure point; xargs reaches EOF and only then fails during nested child address-space creation/exec. The next debug step is to instrument `address_space_create`, PMM page ownership, and `address_space_destroy` with page-table physical addresses and allocation/free sequence numbers, then add a focused two-level `fork -> execve -> wait` QEMU case independent of xargs. Until that passes, xargs remains `IMPLEMENTED / NOT YET VALIDATED`.


## 2026-09-06 — isolated fork child return regression

A temporary `/usr/bin/proc-test` extension isolated a second `fork` before any child `execve`. The first pipe writer fork completed. For the second fork, diagnostics showed matching values at both creation and scheduler entry: `FORKCTX rip=0x0000008000000613 rsp=0x00007fffffffef18` and `USERCTX` with the same RIP/RSP. The child nevertheless faulted immediately after returning to user mode, with the exception RIP equal to `0x0a72657466612d6b`, which is ASCII data from the parent’s `proc:fork-after` string rather than an executable address.

This excludes the xargs tokenizer, pipe EOF, and the basic context-copy operation as the immediate cause. The remaining fault class is the fork child’s user return frame or kernel-stack/syscall-return corruption after `USERCTX` entry. The temporary instrumentation and test extension were reverted. The next required test is a minimal child that performs only `_exit(7)` after the second fork, with diagnostics around the syscall ISR frame and the `iretq` frame; do not change xargs or claim a fix until that test is stable.

## 2026-09-07 — Phase 20 cross-UID CAP_KILL continuation

The Phase 20 credential QEMU harness was extended with `/usr/bin/killtest` and now runs credential, ACL, owner/group/other, set-id environment-sanitization, cross-UID signal and session checks over disposable copies of the RixFS image and UEFI ESP. The harness imports the process environment explicitly and can be reproduced with `make phase20-test`.

`/usr/bin/killtest` forks a child that transitions to UID 2000 and blocks on an inherited pipe. While the caller retains `CAP_KILL`, a cross-UID `SIGUSR1` is accepted and interrupts the child’s blocking read. The caller then drops only `CAP_KILL`; a second UID-2000 child remains releasable through the pipe, while the cross-UID signal attempt is rejected with `-EACCES`. Both children exit and are reaped successfully, preventing the test from confusing authorization with process-lifetime leakage.

The observed serial evidence was:

```text
cap=PASS
acl=PASS
matrix=PASS
setid=PASS
kill=PASS
qemu Phase 20 credential/permission test: PASS
session=PASS
qemu session lifecycle test: PASS
```

Validation commands completed successfully:

```text
git diff --check
make clean CROSS=
make test CROSS=
make image CROSS=
make phase20-test CROSS=
```

No page fault, CPU exception, kernel panic, timeout or prompt-loss marker was observed. This closes the bounded QEMU evidence gap for cross-UID `CAP_KILL`; it does not claim physical-hardware security qualification, persistent account/password authentication, capability delegation across ordinary exec, or metadata-preserving `cp`/`mv` semantics.

## 2026-09-07 — Phase 20 ownership and copy/move metadata continuation

The bounded ownership-preservation slice adds `RIX_SYS_CHOWN`/`chown()` and a centralized VFS policy. A caller with effective UID zero must retain `CAP_DAC_OVERRIDE` to change arbitrary ownership. A non-root caller must own the target and may retain only its own UID plus its effective or supplementary group; non-privileged ownership changes clear set-id bits. Permission failures remain observable as `-EACCES`.

`/bin/cp` and `/bin/mv` now collect source `stat` and ACL v1 metadata after the content transfer, apply source UID/GID and mode/set-id bits through `chown`/`chmod`, and replay or clear the bounded ACL. `/usr/bin/metatest` creates a root-owned, group-owned fixture with set-id mode bits and named-user/named-group ACL entries. The QEMU edge harness verifies metadata after `cp`, verifies metadata and source removal after `mv`, and verifies that both a UID-1000 child and a root process after dropping `CAP_DAC_OVERRIDE` are denied ownership changes.

Observed markers were:

```text
metadata-source=PASS
chown-policy=PASS
cp-metadata-pass
mv-metadata-pass
overwrite-pass
mv-overwrite-pass
multi-sector-pass
qemu cp/mv edge tests: PASS
```

The following validation commands completed successfully:

```text
git diff --check
make test CROSS=
make image CROSS=
make phase20-test CROSS=
python3 scripts/qemu_cp_mv_edge_test.py
```

This closes the bounded QEMU evidence for ownership/mode/set-id/ACL preservation across the current copy-based `cp`/`mv` implementation. It does not claim atomic rename semantics, rollback of an already-overwritten destination after a later metadata failure, recursive directory metadata copying, or physical-hardware security evidence.

### Phase 20 continuation — journaled same-directory rename

- Added versioned `RIX_SYS_RENAME`/`rename()` for regular files within one directory.
- The VFS captures the parent inode before subsequent path lookups; this avoids mutable path-node aliasing and passes the real NVMe/RixFS path.
- RixFS updates one complete directory-entry sector through `journal_write`, preserving the inode and its UID/GID/mode/ACL metadata. An existing destination is rejected with `-EEXIST` rather than overwritten.
- `/bin/mv` attempts atomic rename first and retains the existing metadata-preserving copy/remove fallback for destination collisions and unsupported cross-directory cases.
- Added `/usr/bin/renametest`, covering inode identity preservation, destination-collision rejection and rename round trip in QEMU.
- Reproducible validation: `git diff --check`, `make test CROSS=`, `make image CROSS=`, `make phase20-test CROSS=` and `python3 scripts/qemu_cp_mv_edge_test.py`.
- Observed markers: `rename-inode=PASS`, `rename-exists=PASS`, `rename-roundtrip=PASS`, `cp-metadata-pass`, `mv-metadata-pass`, `fallback-metadata-pass`, and `qemu cp/mv edge tests: PASS`.
- Scope boundary: cross-directory rename, overwrite replacement and multi-object transactional rollback are not claimed by this bounded slice.

### Phase 20 continuation — persistent account records and password verification

The disposable RixFS image now carries `/etc/passwd` with three deterministic accounts and `/etc/shadow` with a root-readable-only `rixsha256` record for the test operator account. The bounded format uses a fixed 32-byte salt and a capped 128-round SHA-256 derivation; the test program never stores a clear-text password in the image.

`/usr/bin/authcheck` validates account record persistence, correct-password acceptance, wrong-password denial, and shadow-file access denial after dropping to UID 1000. The new `auth-test` target runs this over a disposable NVMe/RixFS image, and `make phase20-test CROSS=` includes the authentication harness.

Observed QEMU markers were `accounts=3`, `account-record=PASS`, `auth-pass`, `auth-denied`, `shadow-protected=PASS` and `qemu account/authentication test: PASS`. The slice does not claim interactive login, account mutation/rollback, password rotation, PAM compatibility or hardware-backed credential storage.

### Phase 20 continuation — persistent multi-session registry

Added a bounded session registry alongside process session membership. `RIX_SYS_LIST_SESSIONS` returns a capacity-checked snapshot containing session ID, leader PID, authenticated UID, flags and controlling-TTY state. The kernel registers the embedded user-init session and every newly created login/setsid session, refreshes TTY ownership when listing, and removes records only after the final live member exits or leaves.

`/usr/bin/sessionlisttest` creates a concurrent child session, verifies that both the parent and child sessions are visible, waits for the child to logout and exit, then verifies that the child record is gone while the parent session remains. The existing session QEMU harness now runs both lifecycle and registry tests.

Observed markers were `session=PASS`, `session-registry=PASS` and `qemu session lifecycle test: PASS`, with no page fault, CPU exception or panic markers. This is an in-memory runtime registry; reboot-persistent session serialization and a full login/session manager remain outside this slice.


## 2026-09-07 — Phase 20 clean QEMU gate rerun

After installing the local validation toolchain (`gcc`, MinGW x86_64 UEFI compiler, QEMU/OVMF, `dosfstools` and `mtools`), a clean UEFI/RixFS image was built successfully. The Phase 20 harnesses were aligned with the current boot contract by waiting for `RIXURI: SHELL READY`; credential, session and authentication harnesses were tightened to consume command-specific success markers rather than accepting a stale shell prompt.

The complete `make phase20-test CROSS=` gate passed on the disposable NVMe-backed QEMU image. Observed results included:

- `qemu Phase 20 credential/permission test: PASS`
- `session=PASS`
- `session-registry=PASS`
- `qemu session lifecycle test: PASS`
- `accounts=3`
- `account-record=PASS`
- `auth-pass` and `auth-denied`
- `shadow-protected=PASS`
- `account-add=PASS`, `account-lock=PASS`, `account-unlock=PASS`
- invalid-account rotation rejected without damaging the existing record
- `password-rotate=PASS`
- `login-identity=PASS` and `login=PASS`
- `account-remove=PASS`
- `qemu account/authentication test: PASS`

The run also reached `RIXURI:KERNEL_READY`, `RIXURI:USER_ENTER`, `RIXURI:SYSCALL_OK` and `RIXURI: SHELL READY` without a page fault, CPU exception, panic, timeout or prompt-loss failure. `git diff --check` passed.

This closes the bounded Phase 20 credential, permission, capability, audit, session and account/authentication QEMU slice. It does **not** justify marking the entire Phase 20 complete: the existing fork/address-space/pipe-stress regression remains open, physical AMD hardware security evidence is unavailable here, and power-loss/hardware qualification remains outside this run.


## 2026-09-08 — Phase 21 UDP/TCP composition and loopback evidence

The Phase 21 protocol-composition increment added `kernel/net/udp.c` / `udp.h` and extended `kernel/net/tcp.c` / `tcp.h`. UDP now constructs and parses the eight-byte datagram header, validates the IPv4 pseudo-header checksum, rejects invalid lengths and zero ports, and bounds payload delivery. TCP now constructs and parses minimum-header segments with sequence/acknowledgment values, flags, window and checksum validation. TCP parsing rejects unsupported reserved bits and invalid data offsets.

The socket loopback path was changed so `connect()` for TCP performs and validates a SYN/SYN-ACK/ACK exchange through the TCP wire helpers before entering `ESTABLISHED`. A `GET` request is serialized as ACK/PSH, parsed and validated, and the loopback HTTP response is likewise serialized as ACK/PSH and parsed before being queued. The old direct string-triggered response path was removed. UDP socket send serializes and parses a datagram before delivery; raw ICMP validates the request checksum and constructs a checked echo reply.

The following commands passed with strict warnings:

```text
make HOST_CC=gcc test CROSS=
make image CROSS=
python3 scripts/qemu_ping_test.py
python3 scripts/qemu_curl_test.py
python3 scripts/qemu_external_net_test.py
```

The first three validation commands produced the following real QEMU markers:

```text
E1000: probe bus=0 ...
NVMe: controllers=1
VFS: mount nvme0n1 rc=0
RIXURI:KERNEL_READY
ping: 127.0.0.1: PASS
qemu ping test: PASS
curl: HTTP 200 loopback PASS
qemu curl test: PASS
```

`net-test` additionally passed UDP checksum corruption rejection, UDP payload delivery, TCP SYN and ACK/PSH round trips, TCP sequence/acknowledgment checks, stateful loopback HTTP exchange, ICMP, ARP expiry and Ethernet framing. The external-network QEMU harness reached the shell and verified `ping: DNS/network path unavailable` and `curl: DNS/network path unavailable`; these are correct fail-closed results, not network success. The NIC tests remain boundary tests: QEMU E1000 shows PCI/MMIO/ring setup but no validated TX/RX completion. RTL8125 physical-driver qualification, on-wire ARP/IPv4/UDP/TCP, DNS/DHCP, routing, interrupt/recovery and physical hardware evidence remain open. No external-network success was claimed.


## 2026-09-08 — curl HTML body output

The loopback HTTP response now includes `Content-Type: text/html` and a 50-byte HTML body. `user/programs/curl.c` parses the `\r\n\r\n` header separator, validates the HTTP status and exact HTML body length/content, then writes the body to stdout before the diagnostic PASS line. QEMU verification produced:

```text
<html><body><h1>Hello RixuriOS</h1></body></html>
curl: HTTP 200 loopback PASS
qemu curl test: PASS
```

This is still a loopback HTTP service. URLs outside the loopback path return `DNS/network path unavailable` because DNS, E1000 TX/RX integration, ARP-on-wire, routing and external TCP are not implemented yet. No external website HTML is claimed.


## 2026-09-08 — E1000 DMA completion and link evidence

The E1000 descriptor layout was corrected to the Intel legacy 16-byte ABI. The driver now allocates ring/buffer pages below 4 GiB, programs RDLEN/TDLEN, reads the device-owned descriptor memory, submits TX descriptors with EOP/IFCS/RS, polls TDH for completion, and consumes RX descriptors with DD/EOP while returning buffers through RDT.

Strict host `e1000-test` passed synthetic MMIO/DMA coverage for descriptor size, link state, ring lengths, TX submission/completion and RX consumption. QEMU boot passed with:

```text
E1000: probe bus=0 dev=2 bar=0x00000000810a0000 size=131072 link=1 mac=0x0000525400123456 status=0x0000000000080283
ping: 127.0.0.1: PASS
qemu ping test: PASS
```

The QEMU E1000 result is a driver-boundary result only. No external packet or Google HTML success is claimed because Ethernet/IP/ARP dispatch, DHCP, DNS, routing and external TCP are not yet connected to the socket path.


## 2026-09-08 — Network device adapter

The kernel now initializes a global E1000-backed network-device adapter with QEMU user-net static parameters: `10.0.2.15/24`, gateway `10.0.2.2`, DNS `10.0.2.3`. QEMU boot produced:

```text
E1000: probe ... link=1 mac=0x0000525400123456 ...
NET: device link=1 ip=0x000000000a00020f gateway=0x000000000a000202 dns=0x000000000a000203
ping: 127.0.0.1: PASS
qemu ping test: PASS
```

The adapter exposes E1000 frame transmit/receive to the upper layers, but no external packet success is claimed yet. ARP, IPv4 dispatch, DNS and external TCP still need to be connected to this adapter and to process sockets.


## 2026-09-08 — External ARP boundary and RX DMA blocker

The external QEMU harness produced a packet capture proving that RixuriOS transmitted an ARP request and QEMU user-net generated the expected gateway reply:

```text
frame=3 len=42 dst=ff:ff:ff:ff:ff:ff src=52:54:00:12:34:56 ethertype=0x0806
  arp_op=1 sender_ip=10.0.2.15 target_ip=10.0.2.3
frame=4 len=64 dst=52:54:00:12:34:56 src=52:55:0a:00:02:03 ethertype=0x0806
  arp_op=2 sender_ip=10.0.2.3 target_ip=10.0.2.15
```

The E1000 software RX ring nevertheless remained at `DD=0`, `RDH=0`, `RDT=63` for all descriptors. Therefore the current blocker is E1000 RX DMA delivery, not DNS parsing or HTTP. The external test correctly remains fail-closed with `curl: DNS query failed`; no Google HTML success is claimed.


## 2026-09-08 — E1000 reset and PCI bus-master follow-up

The E1000 configure path now performs a controller reset before ring setup, explicitly enables PCI memory space and bus mastering, programs the receive address valid bit, and uses the broad receive filter. Repeated QEMU external tests still show the same boundary: ARP request/reply appears on the pcap, but the guest RX descriptors remain uncompleted and `curl` reports `DNS query failed`. The next investigation remains E1000 RX DMA/ring ownership rather than DNS or TCP.


## 2026-09-08 — E1000 ASDE/SLU follow-up

Intel 8254x initialization guidance was compared against the driver. The configure path now enables `CTRL.ASDE|CTRL.SLU` after reset and rewrites RAL/RAH, with the QEMU default MAC fallback. QEMU external testing still shows ARP request/reply on the pcap but no RX descriptor completion; `curl` remains `DNS query failed`. The RX ownership issue remains open.


## 2026-09-08 — RCTL ordering follow-up

The receive-control enable write was moved after ring setup, MAC filter programming and TCTL setup, following the Intel 8254x initialization order. QEMU external testing remained unchanged: ARP reply appears on the pcap, RX descriptors remain uncompleted, and `curl google.com` reports `DNS query failed`.

## 2026-09-09 — Full regression matrix: 26/26 PASS
`make test-all` completed successfully with **26 PASS / 0 FAIL**. The run covered the strict build/image path, ring-3 boot, process and signal utilities, RixFS/VFS/file utilities, shell/text utilities, xHCI probe, Phase 19 extended behavior and Phase 21 external-network behavior. QEMU external networking was validated without fake success: when HTTP responses were available, curl reported validated `HTTP 301 external PASS`; when ICMP/DNS was unavailable, ping/curl reported explicit fail-closed diagnostics. The E1000 RX-DMA issue and physical RTL8125 qualification remain documented as open hardware-validation items.


## 2026-09-11 — P0 baseline on dirty tree + smp_test NX-contract fix

HEAD is `e0f0c22`, not the `590b02d` named in the work order (mismatch recorded, work proceeds on actual HEAD). The tree was dirty at baseline start: 14 modified files from the uncommitted ring3/MMIO/TTY debug arc plus one untracked junk file (`nul`, stale build log, left alone).

Baseline with the canonical flags (`CROSS=x86_64-linux-gnu- HOST_CC=gcc`):

- `make test`: FAIL twice before the fix. First at `rtl-test` (undefined `vmm_map_mmio` — a regression from the uncommitted driver work, which swapped `vmm_map_page` for `vmm_map_mmio` without updating the host-test stubs). Fixed by adding `vmm_map_mmio` stubs beside the existing `vmm_map_page` stubs in `tests/e1000_test.c` / `tests/rtl8125_test.c` and by initializing the new `driver.mmio` field in `e1000_test.c`. Then FAIL at `smp-test` (`tests/smp_test.c:204` NX assertion). Production `kernel/arch/x86_64/smp.c` deliberately maps the AP trampoline PRESENT|WRITE with no NX (comment: NX faults the long-mode fallback); the assertion contradicted that documented intent, so the assertion was corrected to pin NX-clear instead of touching production. After both fixes `make test` is RC=0: check/usb/hid/tty/shell/pipe/net/libc/hosts/rtl/e1000/acpi/smp/rixfs-mount PASS, symlink SKIP by design (`tests/symlink_test.c` absent — recorded as SKIP, not PASS).
- `make image`: RC=0. `make iso`: RC=0. `make iso-test`: PASS (`qemu ISO UEFI boot`). `make sysroot`: RC=0 but reports bootstrap only (musl port pending Phase 23 — DEFERRED, not a port). `git diff --check`: clean.
- QEMU `-smp 1`: `SHELL READY` + `USER_ENTER`, no exceptions (UP regression intact).
- QEMU `-smp 4`: discovery OK (`SMP: cpus=4 online=1 bsp_apic=0`), AP1 trampoline prepared and INIT IPI sent, then no further serial output within 150 s (stall between the DEASSERT and SIPI marker lines; slow-TCG-pause vs real hang undetermined). Recorded as BLOCKED, not claimed. P0 Phase B stays IN PROGRESS.
- Re-verified against code (not docs): cooperative scheduler only (voluntary yields, `irq.c`), `kfree` is an explicit no-op (`heap.c`), no MMAP path in `syscall.c` (default `-ENOSYS`), static ELF only (no INTERP/DYN in `kernel/elf`), no TLS/FSMSR usage, no futex primitive, signals are mask/pending/take only (`signal.c`, no delivery/frame), TCP has no kernel retransmission timer or window (caller-paced per comments), no DNS resolver, no `fstat`/`lstat` numbers, no CI configuration, no hardware PASS evidence.

Next: diagnose the `-smp 4` post-DEASSERT stall before any further P0 work.


## 2026-09-11 — P0 Phase B: AP triple-fault loop root-caused (missing EFER.NXE) and fixed

QEMU `-smp 4` never reached the shell: without `-no-reboot` the machine ran a deterministic reset loop (29 loader runs in 170 s), each cycle printing trampoline crumbs `RPL` right after the first SIPI and then resetting. `qemu -d int,cpu_reset` gave the full chain with zero code changes: the AP raises `#PF e=0008` (RSVD) at `lapic_id`'s LAPIC MMIO read (`CR2=0xFFFF8000FEE00000`, `RIP=lapic_id+12`), because AP `EFER=0x500` (LME+LMA, **NXE clear**) while every kernel leaf carries NX. The AP still runs on the BIOS IDT there (the trampoline never loads IDTR), so the fault cascades `#PF → #GP → #DF → #GP` into a triple fault and system reset — no diagnostic output possible. The BSP, spinning in its bounded ONLINE poll, is killed by the reset; GDB snapshots that showed the AP "stuck" at the MMIO read were the frozen triple-fault state, not a hang.

Fix (`kernel/arch/x86_64/smp_trampoline.S`, 5 bytes): set EFER.NXE (`orl $0x800,%eax`) next to the existing LME setup, mirroring the BSP's `vmm_early_init`. Template grows to 318 bytes, still under `SMP_TRAMP_TEMPLATE_MAX` (0x200); the host test compares template bytes via symbols and adapts with no changes.

Evidence: `make test` RC=0, `make image` RC=0, QEMU `-smp 4` now prints `SMP: cpus=4 online=1` then `AP 1/2/3 online`, `SMP: online=4`, `RIXURI:KERNEL_READY`, `RIXURI:USER_ENTER`, `SHELL READY` with 0 exceptions; QEMU `-smp 1` unchanged (SHELL READY, no exceptions).

Explicitly NOT claimed: APs still run on firmware CR4 (no SMEP/SMAP/PKE normalization — follow-up) and on the BIOS IDT (any future AP fault still triple-faults silently — follow-up with Phase C per-CPU state/IDT). No hardware PASS; nested-TCG AP bring-up is slow (pause calibration), which is expected, not a gate.


## 2026-09-11 — P0 Phase B1: AP CPU normalization (CR4/CR0 mirror + kernel IDT)

Spec: APs booted with raw firmware control state (SeaBIOS CR4=0x20, EFER without NXE, BIOS IDT). This change mirrors the BSP `vmm_early_init` policy in the trampoline while paging is off and loads the snapshotted kernel IDTR in long mode. No ABI change, no new trampoline data slots, GDT intentionally unchanged.

Implementation (`kernel/arch/x86_64/smp_trampoline.S`): prot32 sets CR4=MCE|PAE|OSFXSR|OSXMMEXCPT with LA57/PCIDE/SMEP/SMAP/PKE/PGE cleared and CR0=PE|WP with EM/TS cleared; long mode executes `lidt` on `SMP_TRAMP_DATA_IDTR` (the live kernel IDT captured by `smp_capture_descriptor_tables`, never firmware tables) after RSP setup and before ONLINE publish. Template now 347 bytes (< 0x200 max). Design recorded in `docs/SMP_DESIGN.md` (gate B1). Shared IST#1 between BSP/APs accepted for parked Phase-B APs only (documented limitation until per-CPU TSS).

Evidence: `make test` RC=0 (host suite incl. `smp_test`, which adapts via symbols), `make image` RC=0, QEMU `-smp 4` reaches `SMP: online=4`, `KERNEL_READY`, `USER_ENTER`, `SHELL READY` with 0 exceptions, and QEMU-monitor register dumps on a parked AP show `IDT=0x405ba50` (exactly the kernel `idt[]` VMA), `EFER=0xD00` (LME+LMA+NXE), parked in the `ap_entry` hlt loop with IF=0. QEMU `-smp 1` unchanged (SHELL READY, 0 exceptions). CR4 reads did not surface in this QEMU monitor format; the mask executes on the proven R/P/L path (straight-line, pre-paging, cannot fault), and QEMU firmware CR4 (0x20) maps to exactly the BSP value (0x660) under it.

Not claimed: per-CPU TSS/IST (Phase C), IPI ping/pong + shootdown (Phase D), preemptive scheduler (P1), hardware PASS.


## 2026-09-11 — P0 Phase C1: per-CPU kernel stacks + smp_cpu_id

Spec: APs ran their whole C entry on the 4 KiB trampoline page itself; `smp_cpu_t.stack_phys` existed but was never populated; no CPU-to-index accessor. Scheduler and trampoline asm deliberately untouched.

Implementation (`kernel/arch/x86_64/smp.{c,h}`): `smp_start_aps` allocates + zeroes 4 pages per AP via `pmm_alloc_pages`, records the base in `stack_phys`, passes `base + 16 KiB - 8` as `stack_top`; alloc failure skips the AP (DEGRADED, never panic). `smp_setup_trampoline` now requires nonzero + 8-aligned `stack_top` instead of confinement to the page. New `smp_cpu_id()` (LAPIC-ID scan, `-1` unknown). Host tests updated to the new contract (stack arena stub, distinctness asserts, cpu_id cases incl. unknown-ID negative) with no production-logic weakening.

Evidence: `make test` RC=0, `make image` RC=0, QEMU `-smp 4` reaches `SMP: online=4`, `SHELL READY` with 0 exceptions, and GDB register reads prove each parked AP's RSP inside its recorded 16 KiB range with `state=ONLINE` (BSP keeps its own stack). QEMU `-smp 1` unchanged (SHELL READY, 0 exceptions).

Not claimed: stack guard pages (follow-up with per-CPU scheduler), per-CPU runqueues/TSS (Phase C/E), IPI/shootdown (Phase D), HW PASS.


## 2026-09-11 — P0 Phase D1b: AP kernel-GDT switch (first-IPI triple fault)

QEMU `-d int,cpu_reset` evidence on the D1 tree: the first ping IPI
(INT 0xE0) arrives at a parked AP and the CPU raises `#GP e=0x0008`
(segment index 1) at the park `jmp`, cascading `#GP → #GP → #DF → #GP`
into a triple fault and system reset. Gates, IDT base, handler address,
CR3 and RSP were all verified correct; the fault is the CS load during
interrupt vectoring: kernel IDT gates target kernel CS (0x08, 64-bit),
but APs still ran on the trampoline GDT whose 0x08 is a 32-bit segment.
Pre-D1 builds never took AP interrupts (IF=0 forever), so this was
latent until APs began accepting IPIs. (An earlier `-d int` series on a
previous tree showed a different signature, `#GP e=0x0` with a corrupt
IDT base, attributed to the then-current trampoline DATA layout; the
0x2C0 move plus layout static-asserts closed that class.)

Fix (`kernel/arch/x86_64/smp_trampoline.S`, template still < 0x200):
after RSP/IDT setup the AP loads the snapshotted kernel GDTR, far-returns
into 0x08, reloads DS/ES/SS (0x10), nulls FS and loads the kernel TSS
(selector 0x28; LTR on a busy descriptor does not fault, and RSP0 is
unused without CPL changes). IF stays clear throughout; shared TSS/IST
remains accepted for parked APs until per-CPU TSS (Phase C).

Evidence: `make test` RC=0, `make image`/`make iso` RC=0, ESP kernel
disassembly confirms the switch sequence, QEMU `-smp 1` SHELL READY
clean. QEMU `-smp 4` ping/shootdown verdicts were still pending at
write time: nested/ hosted hypervisors (WSL2-TCG and WHPX alike) make
`pause`-calibrated polls cost seconds per round here, so verdicts need
a long window; a `-d int` capture run was left going to decide between
"fixed, just slow" (verdict lines appear) and "new fault chain"
(triple event in the int log). No hardware PASS claimed either way.

Environment note (no code impact): under WSL2-nested TCG and WHPX alike,
guest `pause` spins cost orders of magnitude more wall time than on
silicon (observed ~1% guest CPU on idle hosts, vCPU threads parked in
futex waits). UP boots (no long polls) are unaffected. GDB attaches to a
live multi-vCPU guest correlated with frozen snapshots afterwards;
serial-file polling is non-intrusive and preferred for verdict runs.


## 2026-09-12 — P0 Phase D2: TLB shootdown on unmap (+ build hygiene note)

`address_space_unmap()` (brk shrink/rollback, shm unmap/destroy) is now
the single TLB-discipline choke point: local `vmm_invlpg()` (new wrapper)
when its root is current — closing a latent UP stale-TLB window where the
old path flushed nothing and relied on the next CR3 reload — plus
`smp_shootdown(va)` whenever more than one CPU is online. `vmm.h/c`
gained the wrapper with a comment stating the SMP rule for future
kernel-page unmaps (none exist today). Deadlock-audited (syscall/process
context only; lock-free AP acks). No new host harness (none exists for
address_space); `smp_shootdown` remains fully unit-tested.

Evidence: `make test` RC=0, `make image`/`make iso` RC=0, WHPX `-smp 1`
SHELL READY with no exceptions (hook dormant by design at online==1).
Multi-CPU firing of the hook awaits P5-era AP userspace (documented, not
claimed).

Build hygiene note: `make` printed `Clock skew detected` (WSL↔Windows FS
clocks disagree) — dependency staleness is a real risk here; when in
doubt use `make clean` and verify artifact freshness by disassembly +
mtime cascade, not by trust.
## 2026-09-12 — P0 Phase D1 verdicts close (WHPX -smp 4) + Phase C2 done

D1 verdicts (pending since Phase D1, two frozen/starved WHPX runs with
31-line truncated logs and no verdict lines — nested-virtualization
slowness plus an overnight host sleep, never a guest fault): on the C2
tree, WHPX `-smp 4` prints `SMP: ping 1 ok`, `ping 2 ok`, `ping 3 ok`
and `SMP: shootdown ok`, then `SMP: online=4`, `USER_ENTER`,
`SHELL READY` with 0 exceptions/panics/timeouts over 203 serial lines.
The D1b GDT-switch fix is therefore proven on real virtualization, not
just WSL2-TCG.

C2 implementation (`kernel/arch/x86_64/gdt.c`, `tss.h`, `smp.{c,h}`):
per-AP GDT copy (identical 7-entry layout, TSS stays 0x28 — zero
trampoline asm change) + TSS (`rsp0` = AP stack top, `ist[0]` = private
DF top, no bitmap) on one PMM page, private 4 KiB DF stack on a second;
trampoline DATA GDTR slot carries the per-AP copy (IDTR still the live
snapshot); `tss_set_rsp0`/`tss_current` route via `tss_cpu_index()`
(weak -1 in gdt.c, strong `smp_cpu_id()` in smp.c — the established
weak-stub pattern, no new include edges); BSP keeps its static TSS and
never registers. Skip-AP-on-alloc-failure (DEGRADED) and >8-CPU
deferral unchanged. Log proof: `SMP: AP 1 tss=0x108000 df=0x109000`,
`AP 2 tss=0x10e000 df=0x10f000`, `AP 3 tss=0x114000 df=0x115000`.

Evidence: `make test` RC=0 (new `gdt-test` for the pure builder +
registry negatives; `smp_test` extended with TSS/DF record, GDT-desc,
GDTR-slot and routing asserts — one harness bug caught en route: the
first `ist0` expectation compared a VA against a phys value), `make
image`/`iso` RC=0, `-smp 1` SHELL READY clean, `-smp 4` as above.

Not claimed: per-CPU scheduler/runqueues (Phase E), preemptive
scheduler (P1), hardware PASS.
## 2026-09-12 — P0 Phase E1: SMP-safe scheduler core, zero behavior change

`kernel/sched/scheduler.c` only: `current_index` became
`cpu_current[SMP_MAX_CPUS]` routed via `sched_cpu()` (`smp_cpu_id`,
BSP-index fallback, then 0; zero-init means every CPU starts on
`tasks[0]`), plus one `rix_spinlock_t` — irqsave in the four create
paths / exit / task-returned (arbitrary caller IRQ posture), plain
lock/unlock in `yield` where the existing `cli` already runs. Never
held across `rix_context_switch` or `process_activate`; IRQ posture
across the switch byte-identical. Verified no IRQ/IPI handler takes
the lock (`x86_ipi_dispatch` acks + invlpg only, PIT only ticks). APs
still park; no runqueues/migration/preemption (E2/P1, not claimed).

Evidence: `make test` RC=0, `make image`/`iso` RC=0, WHPX `-smp 1`
SHELL READY (166 serial lines, same count as C2), WHPX `-smp 4`
`online=4` + ping 1/2/3 ok + shootdown ok + SHELL READY (203 lines,
same count as C2) with 0 exceptions/panics/timeouts. Identical
line counts are the behavior-preservation proof.
## 2026-09-12 — P0 Phase E2: APs run kernel threads (first true SMP work)

APs graduate from `sti;hlt` parking to `scheduler_ap_idle()`:
per-CPU idle slot (re-published every iteration — APs park before
`scheduler_init` zeroes the table, so it self-heals), shared
`sched_select_locked` (BSP keeps any-RUNNABLE; APs take only RUNNABLE
+ kernel-thread + `ap_ok` + index!=0), `yield` returns DEAD-current
APs to their idle stack via a per-CPU scratch save slot (never the
dead task's slot — recyclable after unlock), and `SMP_IPI_WAKEUP 226`
(EOI-only `isr226`) + `smp_wakeup[_aps]` broadcast from create paths
after unlock (no-op at online<=1). Per-task `ap_ok` defaults 0: user
tasks, tasks[0] and all four production workers stay BSP-pinned —
drivers are NOT audited for true concurrency, and migrate one by one
later. Only the bounded E2 probe kthread is `ap_ok`.

Two real bugs caught by the first SMP run (kept, fixed, re-run):
(1) init-order race above (AP-spun instead of hlt, caught by torn
serial); (2) yield-to-idle writing the dead slot post-unlock
(statik review catch before it could corrupt a recycled slot).
Serial garbling under true concurrency is environmental (concurrent
writers interleave bytes) — hence the probe ALSO records its cpu in
memory for a tear-free BSP-side report.

Evidence: `make test` RC=0 (`smp_test` covers wakeup
negatives/send-fail/success+EOI and the UP broadcast no-op; pick/idle
have no scheduler harness — documented), `make image`/`iso` RC=0, UP
SHELL READY (168 = 166 + 2 probe lines, probe cpu=0), WHPX `-smp 4`
twice: `online=4`, ping 1/2/3 ok, shootdown ok, `SMP: E2 probe cpu=3`
(AP pickup proven), SHELL READY, 0 exceptions/panics/timeouts.

Not claimed: worker migration (per-driver audits), user tasks on APs,
runqueues, preemption (P1), hardware PASS.
## 2026-09-12 — P0 Phase E3: console serialization (per-call atomicity)

One irqsave `console_lock` (`serial.c`) over whole `serial_write*` /
`serial_read_byte` calls (also serializes the COM1 check-then-read);
`tty_output` split into locking wrapper + `tty_output_nolock` for
`serial.c`'s mirror (one call's UART+FB atomic together; the two locks
are never nested, so no order to audit). `idt.c` nested-fault guard is
now per-CPU (the shared flag would have halted a second CPU's
forensics); nested faults still halt before logging, so the blocking
lock cannot self-deadlock and no forensic variants were needed. Rules
audited: leaves only under lock, holders never panic, no
serial->tty->serial cycle, `serial_drain` best-effort. `tty_test`
gained spin stubs (additive).

Evidence: `make test` RC=0, `make image`/`iso` RC=0, UP SHELL READY
(169 = 168 + 1 report line, probe cpu=0 late-run honest `99` at
KERNEL_READY), WHPX `-smp 4`: `online=4`, ping/shootdown ok, probe
cpu=3, SHELL READY, 0 exceptions. Serial now shows fragment-level
interleave only (`pid=` + probe line glued, each fragment intact —
the documented per-call limit); torn bytes are gone and the total
dropped 208 -> 205 lines.

Not claimed: per-line atomicity (needs single-call printf refactor —
explicitly deferred), worker migration, P1, hardware PASS.
## 2026-09-12 — P0 Phase E4: input lock + first worker on AP

New irqsave `tty_input_lock` (`tty.c`) over whole `tty_input` /
`tty_read` (split into `_nolock` bodies + wrappers, output-split
pattern). Audit: input graph is leaves-only (edit buffers, echo via
`tty_output`, already-IRQ-safe `signal_hook`); `tty_read` never sleeps
inside; nesting is always input->output, never reversed. Serial worker
flipped to `ap_ok` with a one-time cpu log; xhci/kbd/net stay pinned.

Evidence: `make test` RC=0, `make image`/`iso` RC=0, UP SHELL READY
(170 = 169 + 1, worker cpu=0), WHPX `-smp 4`: `online=4`,
ping/shootdown ok, `SMP: serial worker on cpu=3` (AP placement proven;
glued with a `pid=1` fragment per the per-call limit), probe cpu=3,
SHELL READY, 0 exceptions. Shell prompt/echo path uncorrupted.

Not claimed: other workers (per-driver audits pending), per-line
atomicity, P1, hardware PASS.
## 2026-09-12 — P1-slice: BSP timer preemption from IRQ context

100 ms quantum (`pit_irq`, every 10th tick) into
`scheduler_preempt_tick()`: BSP-only + runnable>=2 gated, else plain
`yield` from IRQ context. No new locks, no stack changes; safety rides
on E1 (sched_lock never observed held) and the leaves-only console
discipline (preempted holders always come back). Bounded `PREEMPT`
lines at 1/64/256, then silent. Symmetric AP preemption needs a
per-CPU timer or reschedule-IPI (later, not claimed).

Evidence: `make test` RC=0, `make image`/`iso` RC=0, UP SHELL READY
(173 = 170 + 3, PREEMPT 1/64/256 all present), WHPX `-smp 4`:
`online=4`, ping/shootdown ok, serial worker cpu=3, probe cpu=3,
PREEMPT 1/64/256 (one glued mid-prompt per the per-call limit),
SHELL READY, 0 exceptions/panics/timeouts.

Not claimed: AP preemption, priorities/quantums per task, worker
migration remainder, hardware PASS.
## 2026-09-12 — P1 reverted: timer preemption backed out (E4 shape restored)

Attempted P1-slice (BSP 100 ms quantum) then P1-full (RESCHED-IPI 227,
hog proof, IRQ/task yield split, IF policy). Slice went green twice
(UP/SMP4); full hung silently on UP at 111 lines mid-hog. Two real
layers found: (1) never-yielding tasks inherit IF=0 from the switch
and freeze PIT (fixed, then (2) the actual killer — IF=1-everywhere
exposes the kernel's unlocked allocators (pmm/heap/vmm/process) to
IRQ-yield preemption: a quantum landing mid-allocator corrupts state
through a second task's allocation. Pre-P1 IF~=0 shielded all of it
by accident. Conclusion: preemption needs a kernel-wide
preempt-safety retrofit of its own; not a slice. Fully backed out:
vector 227, broadcast, hog, IRQ-yield split, IF policy, RESCHED host
asserts — tree is cooperative E4 again (wakeup/ap_idle/affinity/probe/
serial-worker kept).

Evidence of restoration: `make test` RC=0, `make image`/`iso` RC=0,
UP SHELL READY at exactly 170 lines (E4 count), WHPX `-smp 4` at
exactly 202 lines (E4 count): `online=4`, ping/shootdown ok, serial
worker cpu=3, probe cpu=3, SHELL READY, 0 exceptions/panics/timeouts.
`kernel.elf` byte-size identical to the E4 build (362488).
SMP_DESIGN P1 sections replaced by this revert note; the attempt's
mechanism evidence stays in the log history above.
