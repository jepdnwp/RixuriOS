#include "idt.h"
#include <stdint.h>
#include <stddef.h>

struct idt_gate {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed));

struct idt_ptr { uint16_t limit; uint64_t base; } __attribute__((packed));
struct interrupt_frame { uint64_t vector; uint64_t error; uint64_t rip, cs, rflags, rsp, ss; };

extern void isr_default(void);
extern void isr0(void); extern void isr1(void); extern void isr2(void); extern void isr3(void);
extern void isr4(void); extern void isr5(void); extern void isr6(void); extern void isr7(void);
extern void isr8(void); extern void isr9(void); extern void isr10(void); extern void isr11(void);
extern void isr12(void); extern void isr13(void); extern void isr14(void); extern void isr15(void);
extern void isr16(void); extern void isr17(void); extern void isr18(void); extern void isr19(void);
extern void isr20(void); extern void isr21(void); extern void isr22(void); extern void isr23(void);
extern void isr24(void); extern void isr25(void); extern void isr26(void); extern void isr27(void);
extern void isr28(void); extern void isr29(void); extern void isr30(void); extern void isr31(void);

static struct idt_gate idt[256] __attribute__((aligned(16)));

static void set_gate(unsigned vector, void (*handler)(void), uint8_t ist, uint8_t attr) {
    uint64_t address = (uint64_t)(uintptr_t)handler;
    idt[vector].offset_low = (uint16_t)address;
    idt[vector].selector = 0x08;
    idt[vector].ist = ist & 7u;
    idt[vector].type_attr = attr;
    idt[vector].offset_mid = (uint16_t)(address >> 16);
    idt[vector].offset_high = (uint32_t)(address >> 32);
    idt[vector].reserved = 0;
}

static void lidt(const struct idt_ptr *ptr) {
    __asm__ volatile ("lidt (%0)" : : "r"(ptr) : "memory");
}

static void cli(void) { __asm__ volatile ("cli" : : : "memory"); }
static void sti(void) { __asm__ volatile ("sti" : : : "memory"); }

void x86_exception_dispatch(const struct interrupt_frame *frame) {
    (void)frame;
    /* Exceptions are fatal until the scheduler/console can provide structured
       recovery. The common stub has already preserved machine state. */
    cli();
    for (;;) __asm__ volatile ("hlt");
}

void idt_init(void) {
    for (unsigned i = 0; i < 256; ++i) set_gate(i, isr_default, 0, 0x8E);
    void (*exceptions[32])(void) = {
        isr0,isr1,isr2,isr3,isr4,isr5,isr6,isr7,isr8,isr9,isr10,isr11,isr12,isr13,isr14,isr15,
        isr16,isr17,isr18,isr19,isr20,isr21,isr22,isr23,isr24,isr25,isr26,isr27,isr28,isr29,isr30,isr31
    };
    for (unsigned i = 0; i < 32; ++i) set_gate(i, exceptions[i], 0, 0x8E);
    struct idt_ptr ptr = { (uint16_t)(sizeof(idt) - 1U), (uint64_t)(uintptr_t)idt };
    lidt(&ptr);
}

void idt_enable(void) { sti(); }
void idt_disable(void) { cli(); }
