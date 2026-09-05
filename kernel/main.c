#include "kernel.h"
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "mm/heap.h"
#include "arch/x86_64/gdt.h"
#include "arch/x86_64/idt.h"

static void halt_forever(void) {
    for (;;) __asm__ volatile ("hlt");
}

void kernel_main(const rixuri_boot_info_t *boot) {
    serial_init();
    serial_write("RixuriOS kernel: x86_64 / AMD64 64-bit\r\n");

    if (!boot || boot->magic != RIXURI_BOOT_MAGIC ||
        boot->version != RIXURI_BOOT_VERSION || boot->size < sizeof(*boot)) {
        panic("invalid UEFI boot handoff");
    }
    if (!boot->memory_map || !boot->memory_descriptor_size || !boot->memory_map_size) {
        panic("missing UEFI memory map");
    }

    serial_write("Boot handoff: version=");
    serial_write_dec(boot->version);
    serial_write(" size=");
    serial_write_dec(boot->size);
    serial_write("\r\n");
    serial_write("ACPI RSDP: "); serial_write_hex(boot->rsdp); serial_write("\r\n");
    serial_write("Framebuffer: "); serial_write_hex(boot->framebuffer_base);
    serial_write(" "); serial_write_dec(boot->framebuffer_width);
    serial_write("x"); serial_write_dec(boot->framebuffer_height); serial_write("\r\n");

    gdt_init();
    serial_write("GDT: initialized\r\n");
    idt_init();
    serial_write("IDT: initialized\r\n");

    pmm_init((const void *)(uintptr_t)boot->memory_map,
             boot->memory_map_size,
             boot->memory_descriptor_size,
             boot->kernel_phys_base,
             boot->kernel_phys_end,
             (uint64_t)(uintptr_t)boot,
             sizeof(*boot));
    if (pmm_free_pages() == 0) panic("physical memory allocator has no free pages");
    serial_write("PMM: total="); serial_write_dec(pmm_total_pages());
    serial_write(" free="); serial_write_dec(pmm_free_pages()); serial_write("\r\n");

    vmm_early_init();
    if (vmm_kernel_pml4() == 0) panic("VMM initialization failed");
    serial_write("VMM: early identity map active\r\n");

    heap_init();
    if (!kmalloc(1, sizeof(uintptr_t))) panic("kernel heap initialization failed");
    serial_write("KHEAP: initialized\r\n");

    halt_forever();
}
