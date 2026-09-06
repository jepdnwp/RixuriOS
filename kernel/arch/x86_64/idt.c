#include "idt.h"
#include "kernel.h"
#include "../../mm/ptmap.h"
#include "../../mm/vmm.h"
#include "../../process/process.h"
#include <stdint.h>
#include <stddef.h>

struct idt_gate { uint16_t offset_low; uint16_t selector; uint8_t ist; uint8_t type_attr; uint16_t offset_mid; uint32_t offset_high; uint32_t reserved; } __attribute__((packed));
struct idt_ptr { uint16_t limit; uint64_t base; } __attribute__((packed));
struct interrupt_frame { uint64_t vector; uint64_t error; uint64_t rip, cs, rflags, rsp, ss; };

extern void isr_default(void);
extern void isr128(void);
#define DECL(n) extern void isr##n(void);
DECL(0) DECL(1) DECL(2) DECL(3) DECL(4) DECL(5) DECL(6) DECL(7) DECL(8) DECL(9) DECL(10) DECL(11) DECL(12) DECL(13) DECL(14) DECL(15)
DECL(16) DECL(17) DECL(18) DECL(19) DECL(20) DECL(21) DECL(22) DECL(23) DECL(24) DECL(25) DECL(26) DECL(27) DECL(28) DECL(29) DECL(30) DECL(31)
DECL(32) DECL(33) DECL(34) DECL(35) DECL(36) DECL(37) DECL(38) DECL(39) DECL(40) DECL(41) DECL(42) DECL(43) DECL(44) DECL(45) DECL(46) DECL(47)
#undef DECL

static struct idt_gate idt[256] __attribute__((aligned(16)));
static void set_gate(unsigned vector,void (*handler)(void),uint8_t ist,uint8_t attr){uint64_t address=(uint64_t)(uintptr_t)handler;idt[vector].offset_low=(uint16_t)address;idt[vector].selector=0x08;idt[vector].ist=ist&7u;idt[vector].type_attr=attr;idt[vector].offset_mid=(uint16_t)(address>>16);idt[vector].offset_high=(uint32_t)(address>>32);idt[vector].reserved=0;}
static void lidt(const struct idt_ptr *ptr){__asm__ volatile("lidt (%0)"::"r"(ptr):"memory");}
static void cli(void){__asm__ volatile("cli":::"memory");}
static void sti(void){__asm__ volatile("sti":::"memory");}
static uint64_t read_cr2(void){uint64_t value;__asm__ volatile("mov %%cr2,%0":"=r"(value));return value;}
static void fault_entry(const char *name,uint64_t entry){serial_write(name);serial_write_hex(entry);serial_write("\r\n");}
static void page_fault_diagnostics(const struct interrupt_frame *frame){
    uint64_t va=read_cr2(),cr3=vmm_current_pml4();
    rix_process_t *process=process_lookup(process_current());
    serial_write("PAGE FAULT: pid=");serial_write_dec(process_current());
    serial_write(" parent=");serial_write_dec(process?process->parent:0);
    serial_write(" cr3=");serial_write_hex(cr3);serial_write(" cr2=");serial_write_hex(va);
    serial_write(" rip=");serial_write_hex(frame?frame->rip:0);
    serial_write(" error=");serial_write_hex(frame?frame->error:0);
    serial_write(" as=");serial_write_hex((uint64_t)(uintptr_t)(process?&process->address_space:0));
    serial_write(" pml4_phys=");serial_write_hex(process?process->address_space.pml4_phys:0);serial_write("\r\n");
    uint64_t *pml4=(uint64_t*)pt_kmap(cr3),pml4e=0,pdpte=0,pde=0,pte=0;
    if(pml4){pml4e=pml4[(va>>39)&0x1ffu];if(pml4e){uint64_t *pdpt=(uint64_t*)pt_kmap(pml4e&~0xfffULL);if(pdpt){pdpte=pdpt[(va>>30)&0x1ffu];if(pdpte){uint64_t *pd=(uint64_t*)pt_kmap(pdpte&~0xfffULL);if(pd){pde=pd[(va>>21)&0x1ffu];if(pde&&!(pde&(1ULL<<7))){uint64_t *pt=(uint64_t*)pt_kmap(pde&~0xfffULL);if(pt)pte=pt[(va>>12)&0x1ffu];}}}}}}
    fault_entry("  PML4E=",pml4e);fault_entry("  PDPTE=",pdpte);fault_entry("  PDE=",pde);fault_entry("  PTE=",pte);
    serial_write("  physical_page=");serial_write_hex((pte?pte:pde?pde:pdpte?pdpte:pml4e)&~0xfffULL);serial_write("\r\n");
}
void x86_exception_dispatch(const struct interrupt_frame *frame){if(frame){if(frame->vector==14)page_fault_diagnostics(frame);else{serial_write("CPU exception vector=");serial_write_dec(frame->vector);serial_write(" error=");serial_write_hex(frame->error);serial_write(" rip=");serial_write_hex(frame->rip);serial_write("\r\n");}}cli();for(;;)__asm__ volatile("hlt");}
void idt_init(void){
    for(unsigned i=0;i<256;i++)set_gate(i,isr_default,0,0x8E);
    void (*exceptions[32])(void)={isr0,isr1,isr2,isr3,isr4,isr5,isr6,isr7,isr8,isr9,isr10,isr11,isr12,isr13,isr14,isr15,isr16,isr17,isr18,isr19,isr20,isr21,isr22,isr23,isr24,isr25,isr26,isr27,isr28,isr29,isr30,isr31};
    void (*irqs[16])(void)={isr32,isr33,isr34,isr35,isr36,isr37,isr38,isr39,isr40,isr41,isr42,isr43,isr44,isr45,isr46,isr47};
    for(unsigned i=0;i<32;i++)set_gate(i,exceptions[i],0,0x8E);
    for(unsigned i=0;i<16;i++)set_gate(32+i,irqs[i],0,0x8E);
    set_gate(0x80,isr128,0,0xEE);
    struct idt_ptr ptr={(uint16_t)(sizeof(idt)-1U),(uint64_t)(uintptr_t)idt};lidt(&ptr);
}
void idt_enable(void){sti();}
void idt_disable(void){cli();}
