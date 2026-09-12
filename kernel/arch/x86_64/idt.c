#include "idt.h"
#include "tss.h"
#include "kernel.h"
#include "../../serial.h"
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
extern void isr224(void);
extern void isr225(void);
extern void isr226(void);

static struct idt_gate idt[256] __attribute__((aligned(16)));
static void set_gate(unsigned vector,void (*handler)(void),uint8_t ist,uint8_t attr){uint64_t address=(uint64_t)(uintptr_t)handler;idt[vector].offset_low=(uint16_t)address;idt[vector].selector=0x08;idt[vector].ist=ist&7u;idt[vector].type_attr=attr;idt[vector].offset_mid=(uint16_t)(address>>16);idt[vector].offset_high=(uint32_t)(address>>32);idt[vector].reserved=0;}
static void lidt(const struct idt_ptr *ptr){__asm__ volatile("lidt (%0)"::"r"(ptr):"memory");}
static void cli(void){__asm__ volatile("cli":::"memory");}
static void sti(void){__asm__ volatile("sti":::"memory");}
static uint64_t read_cr2(void){uint64_t value;__asm__ volatile("mov %%cr2,%0":"=r"(value));return value;}
static uint64_t read_cr3_hw(void){uint64_t value;__asm__ volatile("mov %%cr3,%0":"=r"(value)::"memory");return value;}
static void fault_entry(const char *name,uint64_t entry){kernel_log(name);kernel_log_hex(entry);kernel_log("\r\n");}
static void page_fault_diagnostics(const struct interrupt_frame *frame){
    /* Dual-output (screen+serial): physical-console-only setups must see
     * faults too. The framebuffer lives in the shared low identity window,
     * so it is mapped under any process CR3. CR3HW is read from hardware;
     * CR3SW is the software tracker: if they differ, the tracker desynced. */
    uint64_t va=read_cr2(),cr3hw=read_cr3_hw(),cr3=vmm_current_pml4();
    rix_process_t *process=process_lookup(process_current());
    kernel_log("#PF: CR2=");kernel_log_hex(va);
    kernel_log(" RIP=");kernel_log_hex(frame?frame->rip:0);
    kernel_log(" RSP=");kernel_log_hex(frame?frame->rsp:0);
    kernel_log(" CR3HW=");kernel_log_hex(cr3hw);
    kernel_log(" CR3SW=");kernel_log_hex(cr3);
    kernel_log(" error_code=");kernel_log_hex(frame?frame->error:0);
    kernel_log("\r\n");
    kernel_log("PAGE FAULT: pid=");kernel_log_dec(process_current());
    kernel_log(" parent=");kernel_log_dec(process?process->parent:0);
    kernel_log(" cr3=");kernel_log_hex(cr3);kernel_log(" cr2=");kernel_log_hex(va);
    kernel_log(" rip=");kernel_log_hex(frame?frame->rip:0);
    kernel_log(" error=");kernel_log_hex(frame?frame->error:0);
    kernel_log(" as=");kernel_log_hex((uint64_t)(uintptr_t)(process?&process->address_space:0));
    kernel_log(" pml4_phys=");kernel_log_hex(process?process->address_space.pml4_phys:0);kernel_log("\r\n");
    uint64_t *pml4=(uint64_t*)pt_kmap(cr3),pml4e=0,pdpte=0,pde=0,pte=0;
    if(pml4){pml4e=pml4[(va>>39)&0x1ffu];if(pml4e){uint64_t *pdpt=(uint64_t*)pt_kmap(pml4e&~0xfffULL);if(pdpt){pdpte=pdpt[(va>>30)&0x1ffu];if(pdpte){uint64_t *pd=(uint64_t*)pt_kmap(pdpte&~0xfffULL);if(pd){pde=pd[(va>>21)&0x1ffu];if(pde&&!(pde&(1ULL<<7))){uint64_t *pt=(uint64_t*)pt_kmap(pde&~0xfffULL);if(pt)pte=pt[(va>>12)&0x1ffu];}}}}}}
    fault_entry("  PML4E=",pml4e);fault_entry("  PDPTE=",pdpte);fault_entry("  PDE=",pde);fault_entry("  PTE=",pte);
    kernel_log("  physical_page=");kernel_log_hex((pte?pte:pde?pde:pdpte?pdpte:pml4e)&~0xfffULL);kernel_log("\r\n");
}
void scheduler_dump_states(void);
/* HW fault forensics: runs on the IST#1 stack, so it stays usable even when
 * the faulting stack pointer itself is corrupt.  Every probe is a plain
 * read; a probe of an unmapped address raises a nested fault which the
 * in_fault guard turns into a halt (earlier lines are preserved).  Order is
 * deliberate: safe .bss reads first, fault-RSP stack second, fault-RIP
 * bytes last. */
static void fault_hex_byte(uint8_t v){static const char d[]="0123456789abcdef";char c[2];c[0]=d[(v>>4)&0xFu];c[1]=d[v&0xFu];kernel_log_n(c,2);}
static void fault_forensics(const struct interrupt_frame *frame){
    const x86_tss_t *tss=tss_current();
    kernel_log("FAULT: rsp0=");kernel_log_hex(tss?tss->rsp0:0);kernel_log("\r\n");
    scheduler_dump_states();
    /* At ISR entry RSP = fault_RSP - 16 (synthetic vector+error) - 120
     * (saved regs); the dispatch frame sits at RSP+120.  With IST the CPU
     * always pushes SS:RSP, so frame->rsp is meaningful for kernel faults
     * too (the pre-fault kernel RSP).  For user faults the interesting
     * kernel stack is the RSP0 trap stack, not the IST area: dump that.
     * A wild base is only ever READ; an unmapped read nested-faults into
     * the in_fault halt with earlier lines preserved. */
    uint64_t fault_rsp;
    if(frame->cs==0x1bu&&tss&&tss->rsp0)fault_rsp=tss->rsp0>256u?tss->rsp0-256u:0;
    else if(frame->cs!=0x1bu&&frame->rsp>64u)fault_rsp=frame->rsp-64u;
    else fault_rsp=(uint64_t)(uintptr_t)frame+16u;
    kernel_log("FAULT: dump base=");kernel_log_hex(fault_rsp);kernel_log("\r\n");
    for(unsigned i=0;fault_rsp&&i<24u;i++){
        uint64_t addr=fault_rsp+(uint64_t)i*8u;
        /* Stay inside the low-half canonical range; anything else would
         * nested-fault before printing a single row. */
        if(addr>=(1ULL<<47))break;
        kernel_log("STK+");fault_hex_byte((uint8_t)(i*8u));kernel_log("=");
        kernel_log_hex(*(volatile uint64_t*)(uintptr_t)addr);kernel_log("\r\n");
    }
    uint64_t rip=frame?frame->rip:0;
    if(rip&&rip<(1ULL<<47)){
        kernel_log("FAULT: bytes@rip=");
        for(unsigned i=0;i<16u;i++)fault_hex_byte(((volatile uint8_t*)(uintptr_t)rip)[i]);
        kernel_log("\r\n");
    }
}
void x86_exception_dispatch(const struct interrupt_frame *frame){static volatile unsigned in_fault=0;__asm__ volatile("outb %0,%1"::"a"((uint8_t)(frame?frame->vector:0xFFu)),"Nd"((uint16_t)0x80u));if(in_fault){cli();for(;;)__asm__ volatile("hlt");}in_fault=1;if(frame){if(frame->vector==14)page_fault_diagnostics(frame);else{kernel_log("CPU exception vector=");kernel_log_dec(frame->vector);kernel_log(" error=");kernel_log_hex(frame->error);kernel_log(" rip=");kernel_log_hex(frame->rip);kernel_log("\r\n");}kernel_log(" cs=");kernel_log_hex(frame->cs);kernel_log(" rflags=");kernel_log_hex(frame->rflags);kernel_log(" rsp=");kernel_log_hex(frame->rsp);kernel_log(" ss=");kernel_log_hex(frame->ss);kernel_log(" cr3hw=");kernel_log_hex(read_cr3_hw());kernel_log(" cr3sw=");kernel_log_hex(vmm_current_pml4());kernel_log("\r\n");fault_forensics(frame);cr3trace_dump();}serial_drain();cli();for(;;)__asm__ volatile("hlt");}
void idt_init(void){
    for(unsigned i=0;i<256;i++)set_gate(i,isr_default,0,0x8E);
    void (*exceptions[32])(void)={isr0,isr1,isr2,isr3,isr4,isr5,isr6,isr7,isr8,isr9,isr10,isr11,isr12,isr13,isr14,isr15,isr16,isr17,isr18,isr19,isr20,isr21,isr22,isr23,isr24,isr25,isr26,isr27,isr28,isr29,isr30,isr31};
    void (*irqs[16])(void)={isr32,isr33,isr34,isr35,isr36,isr37,isr38,isr39,isr40,isr41,isr42,isr43,isr44,isr45,isr46,isr47};
    for(unsigned i=0;i<32;i++)set_gate(i,exceptions[i],0,0x8E);
    /* Route every CPU exception through IST#1 (the double-fault stack).
     * A fault with a corrupt RSP (wild context switch, smashed kernel
     * stack) must still print diagnostics on physical hardware instead of
     * double-faulting on the broken stack into a silent triple fault.
     * IRQs and int 0x80 keep IST=0: they arrive on sane stacks by design. */
    for(unsigned i=0;i<32;i++)set_gate(i,exceptions[i],1,0x8E);
    set_gate(8,isr8,1,0x8E);
    set_gate(10,isr10,1,0x8E);
    set_gate(11,isr11,1,0x8E);
    set_gate(12,isr12,1,0x8E);
    set_gate(13,isr13,1,0x8E);
    set_gate(14,isr14,1,0x8E);
    for(unsigned i=0;i<16;i++)set_gate(32+i,irqs[i],0,0x8E);
    /* Phase D IPIs: kernel-only gates (DPL0); the AP park loop is the only
     * other CPU that can take them. IST=0 like IRQs: arrival stacks are
     * sane by design (per-CPU kernel stacks, CPL0 park). */
    set_gate(224,isr224,0,0x8E);
    set_gate(225,isr225,0,0x8E);
    set_gate(226,isr226,0,0x8E);
    set_gate(0x80,isr128,0,0xEE);
    struct idt_ptr ptr={(uint16_t)(sizeof(idt)-1U),(uint64_t)(uintptr_t)idt};lidt(&ptr);
}
void idt_enable(void){sti();}
void idt_disable(void){cli();}
