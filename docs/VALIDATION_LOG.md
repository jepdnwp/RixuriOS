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
