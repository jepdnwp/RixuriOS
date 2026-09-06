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

The current implementation keeps one non-control endpoint ring per slot as an explicit incremental boundary. A subsequent step must maintain multiple endpoint contexts/rings simultaneously before a composite HID device or a device with separate bulk and interrupt interfaces can be claimed as integrated.
