# RixuriOS Hardware Target

RixuriOS is a 64-bit-only x86_64/AMD64 OS. The real-hardware acceptance target documented by the project specification is:

- CPU: AMD Ryzen 7 7700
- Discrete GPU: AMD Radeon RX 6800 XT (PCI 1002:73BF)
- iGPU/platform graphics: AMD iGPU as present on the target platform
- NVMe: XPG GAMMIX S70 BLADE
- Ethernet: Realtek RTL8125 (PCI 10EC:8125)
- USB: AMD xHCI
- Platform: ASUS

## Driver policy

Hardware detection is not equivalent to functional support. A driver may report `DETECTED`, but acceptance requires real operations appropriate to the device: storage read/write/flush, USB enumeration and HID input, network packet TX/RX, and graphics/framebuffer validation.

QEMU devices are separate from real hardware. In particular, QEMU's virtual GPU 1B36:0100 must never be treated as an RX 6800 XT, and a QEMU PASS is never a real-hardware PASS.

## Graphics and Vulkan

The graphics architecture is staged:

1. UEFI framebuffer and reliable text/diagnostic output.
2. PCI GPU discovery and device/resource management.
3. AMD display initialization and framebuffer correctness.
4. A real AMD GPU command/queue/memory architecture.
5. A userspace graphics API layer designed for future Vulkan support.
6. Vulkan support only when backed by real GPU functionality and conformance testing; fake acceleration is forbidden.

The pre-GUI milestone does not claim Vulkan support. Vulkan becomes an explicit later milestone after the kernel, memory, DMA, PCI, synchronization, GPU memory management, and userspace ABI are stable.

## Bootable USB media policy

The release image must be a genuine UEFI bootable filesystem/image, not merely a copied kernel binary. Build and release tooling must:

- produce a reproducible disk image;
- contain the required UEFI boot files and kernel;
- use an EFI System Partition layout appropriate for UEFI firmware;
- verify the resulting image before release;
- support writing the image to USB without requiring proprietary tooling;
- never silently format or overwrite a user's disk during installation;
- provide explicit destructive-operation confirmation in the installer.

USB-boot acceptance must be performed on the real ASUS target platform as well as in QEMU. A successful image build alone is not a boot PASS.

## Regression requirements

Historical regressions are permanent tests:

- UEFI `GetMemoryMap()` ABI/structure/function-pointer correctness and no invalid-opcode crash.
- NVMe controller readiness, Identify, namespace online, real read/write/flush.
- xHCI Address Device completion handling, including the previous completion-code-11 failure.
- USB keyboard enumeration/input, including actual key data rather than a synthetic event.
- QEMU GPU 1B36:0100 versus RX 6800 XT 1002:73BF separation.
- RixFS bad-magic/version handling must fail safely and must never auto-format.
- RTL8125 self-test must use the real network path and verify real packet TX/RX.

See the master specification for the full acceptance and regression matrix.
