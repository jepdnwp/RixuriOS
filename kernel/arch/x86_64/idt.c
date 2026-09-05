#include "idt.h"
#include <stdint.h>
#include <stddef.h>

struct idt_gate { uint16_t offset_low; uint16_t selector; uint8_t ist; uint8_t type_attr; uint16_t offset_mid; uint32_t offset_high; uint32_t reserved; } __attribute__((packed));
struct idt_ptr { uint16_t limit; uint64_t base; } __attribute__((packed));
struct interrupt_frame { uint64_t vector; uint64_t error; uint64_t rip, cs, rflags, rsp, ss; };

extern void isr_default(void);
#define DECL(n) extern void isr##n(void);
DECL(0) DECL(1) DECL(2) DECL(3) DECL(4) DECL(5) DECL(6) DECL(7) DECL(8) DECL(9) DECL(10) DECL(11) DECL(12) DECL(13) DECL(14) DECL(15)
DECL(16) DECL(17) DECL(18) DECL(19) DECL(20) DECL(21) DECL(22) DECL(23) DECL(24) DECL(25) DECL(26) DECL(27) DECL(28) DECL(29) DECL(30) DECL(31)
#undef DECL

static struct idt_gate idt[256] __attribute__((aligned(16)));
static void set_gate(unsigned vector, void (*handler)(void), uint8_t ist, uint8_t attr) { uint64_t a=(uint64_t)(uintptr_t)handler; idt[vector].offset_low=(uint16_t)a; idt[vector].selector=0x08; idt[vector].ist=ist&7u; idt[vector].type_attr=attr; idt[vector].offset_mid=(uint16_t)(a>>16); idt[vector].offset_high=(uint32_t)(a>>32); idt[vector].reserved=0; }
static void lidt(const struct idt_ptr *p){__asm__ volatile("lidt (%0)"::"r"(p):"memory");}
static void cli(void){__asm__ volatile("cli":::"memory");} static void sti(void){__asm__ volatile("sti":::"memory");}
void x86_exception_dispatch(const struct interrupt_frame *frame){(void)frame;cli();for(;;)__asm__ volatile("hlt");}
void idt_init(void){void(*e[32])(void)={isr0,isr1,isr2,isr3,isr4,isr5,isr6,isr7,isr8,isr9,isr10,isr11,isr12,isr13,isr14,isr15,isr16,isr17,isr18,isr19,isr20,isr21,isr22,isr23,isr24,isr25,isr26,isr27,isr28,isr29,isr30,isr31};for(unsigned i=0;i<256;++i)set_gate(i,isr_default,0,0x8e);for(unsigned i=0;i<32;++i)set_gate(i,e[i],0,0x8e);struct idt_ptr p={(uint16_t)(sizeof(idt)-1U),(uint64_t)(uintptr_t)idt};lidt(&p);}
void idt_enable(void){sti();} void idt_disable(void){cli();}
