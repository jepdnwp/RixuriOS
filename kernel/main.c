#include "kernel.h"

static void halt_forever(void) {
    for (;;) __asm__ volatile ("hlt");
}

void kernel_main(const rixuri_boot_info_t *boot) {
    serial_init();
    serial_write("RixuriOS kernel: x86_64 / AMD64 64-bit\r\n");

    if (!boot || boot->magic != RIXURI_BOOT_MAGIC) {
        panic("invalid UEFI boot handoff");
    }

    serial_write("UEFI boot handoff: OK\r\n");
    serial_write("RixuriOS kernel foundation reached C17 entry.\r\n");

    halt_forever();
}
