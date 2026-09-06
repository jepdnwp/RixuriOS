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

The linker was hardened during the Phase 0–17 audit. Explicit PHDRS now produce separate `R-X`, `R--` and `RW-` load segments plus a read-only `GNU_STACK`; `readelf -l build/kernel.elf` confirms no `RWE` segment. USB, HID and TTY host tests and the UEFI/QEMU boot smoke test continue to pass after this change.

Review of the composite-device path found and corrected a context-construction defect: each Configure Endpoint operation now updates the input Slot Context's Context Entries field to the highest configured DCI and sets Add Slot Context alongside the endpoint bit. This is required by xHCI when adding endpoints beyond the initial EP0 context; the fix is strict-build validated but still awaits controller-backed execution.
