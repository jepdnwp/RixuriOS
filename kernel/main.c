#include "kernel.h"
#include "mm/pmm.h"
#include "mm/vmm.h"

static void halt_forever(void) {
    for (;;) __asm__ volatile ("hlt");
}

static void report_u64(const char *label, uint64_t value) {
    char buf[32];
    size_t i = sizeof(buf) - 1;
    buf[i] = '\0';
    if (value == 0) {
        serial_write(label);
        serial_write("0\r\n");
        return;
    }
    while (value && i > 0) {
        buf[--i] = (char)('0' + (value % 10));
        value /= 10;
    }
    serial_write(label);
    serial_write(&buf[i]);
    serial_write("\r\n");
}

void kernel_main(const rixuri_boot_info_t *boot) {
    serial_init();
    serial_write("RixuriOS kernel: x86_64 / AMD64 64-bit\r\n");

    if (!boot || boot->magic != RIXURI_BOOT_MAGIC) {
        panic("invalid UEFI boot handoff");
    }
    if (!boot->memory_map || boot->memory_descriptor_size < 40 ||
        boot->memory_map_size < boot->memory_descriptor_size) {
        panic("invalid UEFI memory map");
    }
    if (boot->kernel_phys_end <= boot->kernel_phys_base) {
        panic("invalid kernel physical range");
    }

    serial_write("UEFI boot handoff: OK\r\n");
    serial_write("Initializing physical memory manager...\r\n");
    pmm_init((const void *)(uintptr_t)boot->memory_map,
             boot->memory_map_size,
             boot->memory_descriptor_size,
             boot->kernel_phys_base,
             boot->kernel_phys_end,
             (uint64_t)(uintptr_t)boot,
             sizeof(*boot));
    report_u64("PMM total pages: ", pmm_total_pages());
    report_u64("PMM free pages: ", pmm_free_pages());

    serial_write("Initializing early virtual memory...\r\n");
    vmm_early_init();
    if (vmm_translate(0) != 0) panic("early identity mapping failed");
    if (vmm_translate(0x100000ULL) != 0x100000ULL) panic("kernel identity mapping failed");
    serial_write("Early VMM: OK\r\n");

    serial_write("RixuriOS kernel memory foundation initialized.\r\n");
    halt_forever();
}
