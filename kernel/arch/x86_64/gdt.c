#include "gdt.h"
#include <stdint.h>
#include <stddef.h>

struct gdt_ptr { uint16_t limit; uint64_t base; } __attribute__((packed));

static uint64_t gdt[3] __attribute__((aligned(8))) = {
    0x0000000000000000ULL,
    0x00AF9A000000FFFFULL,
    0x00AF92000000FFFFULL
};

static void gdt_load(const struct gdt_ptr *ptr) {
    __asm__ volatile ("lgdt (%0)" : : "r"(ptr) : "memory");
    __asm__ volatile (
        "pushq $0x08\n"
        "leaq 1f(%%rip), %%rax\n"
        "pushq %%rax\n"
        "lretq\n"
        "1:\n"
        "movw $0x10, %%ax\n"
        "movw %%ax, %%ds\n"
        "movw %%ax, %%es\n"
        "movw %%ax, %%ss\n"
        : : : "rax", "memory");
}

void gdt_init(void) {
    struct gdt_ptr ptr = { (uint16_t)(sizeof(gdt) - 1U), (uint64_t)(uintptr_t)gdt };
    gdt_load(&ptr);
}
