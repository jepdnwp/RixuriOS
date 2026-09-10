#include "smp.h"

    .section .text
    .code16
    .globl smp_trampoline_start
    .type smp_trampoline_start,@function
/* AP real-mode entry: CS = SIPI vector, IP = 0, so CS.base == page base.
 * DS is pointed at CS; every data access below is page-relative and the
 * template stays position-independent except the one patched long jump.
 *
 * Breadcrumbs (reset-proof): the AP announces each stage on COM1 with raw
 * port I/O (no stack, no kernel): R = real, P = protected, L = long mode.
 * Known-answer probes then dump what the BSP wrote: G = CR3 slot,
 * T = entry slot, then jump. A triple fault still leaves the death stage
 * in the serial log. */
smp_trampoline_start:
    cli
    movw %cs,%ax
    movw %ax,%ds
    movb $0x52,%ah
    movw $0x3FD,%dx
1:  inb %dx,%al
    testb $0x20,%al
    jz 1b
    movw $0x3F8,%dx
    movb %ah,%al
    outb %al,%dx
    movb $SMP_TRAMP_CRUMB_REAL,SMP_TRAMP_CRUMB_OFF
    lgdtl SMP_TRAMP_GDTR_OFF
    movl %cr0,%eax
    orb $0x1,%al
    movl %eax,%cr0
    /* Far jump reloads CS from the GDT (base = page) and serializes the
     * pipeline after the CR0.PE write. Offset is page-relative. */
    ljmpl $SMP_TRAMP_SEL_CODE32,$smp_trampoline_prot32-smp_trampoline_start
smp_trampoline_prot32:
    .code32
    movw $SMP_TRAMP_SEL_DATA32,%ax
    movw %ax,%ds
    movw %ax,%es
    movw %ax,%ss
    movw $SMP_TRAMP_SEL_DATAPAGE,%ax
    movw %ax,%fs
    movb $0x50,%ah
    movl $0x3FD,%edx
1:  inb %dx,%al
    testb $0x20,%al
    jz 1b
    movl $0x3F8,%edx
    movb %ah,%al
    outb %al,%dx
    movb $SMP_TRAMP_CRUMB_PROT32,%fs:SMP_TRAMP_CRUMB_OFF
    movl %cr4,%eax
    orl $0x20,%eax
    movl %eax,%cr4
    movl %fs:SMP_TRAMP_DATA_CR3,%eax
    movl %eax,%cr3
    movl $0xC0000080,%ecx
    rdmsr
    orl $0x100,%eax
    wrmsr
    movl %cr0,%eax
    orl $0x80000000,%eax
    movl %eax,%cr0
    /* Long mode forces CS.base = 0, so this offset is patched by the BSP
     * to page + smp_trampoline_longmode. Single patch point: the 4-byte
     * offset field starts 6 bytes before the end of this 7-byte ljmp. */
    ljmpl $SMP_TRAMP_SEL_CODE64,$0x12345678
    .globl smp_trampoline_patch_longjump
smp_trampoline_patch_longjump = . - 6
    .code64
    .globl smp_trampoline_longmode
smp_trampoline_longmode:
    movb $0x4C,%ah
    movl $0x3FD,%edx
1:  inb %dx,%al
    testb $0x20,%al
    jz 1b
    movl $0x3F8,%edx
    movb %ah,%al
    outb %al,%dx
    /* Derive the page base from RIP: no segment, GDT or MSR dependence.
     * R10 keeps the base (call-clobbered but hex64 preserves it). */
    lea smp_trampoline_longmode(%rip),%r10
    sub $smp_trampoline_longmode-smp_trampoline_start,%r10
    movb $0xCC,SMP_TRAMP_CRUMB_OFF(%r10)
    movw $SMP_TRAMP_SEL_DATA64,%ax
    movw %ax,%ds
    movw %ax,%es
    movw %ax,%ss
    /* RSP first: the hex printer below uses call/ret. */
    movq SMP_TRAMP_DATA_STACK(%r10),%rsp
    /* G probe: dump the CR3 slot (BSP wrote the AP PML4). */
    movb $0x47,%ah
    movl $0x3FD,%edx
1:  inb %dx,%al
    testb $0x20,%al
    jz 1b
    movl $0x3F8,%edx
    movb %ah,%al
    outb %al,%dx
    movq SMP_TRAMP_DATA_CR3(%r10),%rcx
    call hex64
    /* T probe: dump the entry slot, then jump to it. */
    movb $0x54,%ah
    movl $0x3FD,%edx
1:  inb %dx,%al
    testb $0x20,%al
    jz 1b
    movl $0x3F8,%edx
    movb %ah,%al
    outb %al,%dx
    movq SMP_TRAMP_DATA_ENTRY(%r10),%rcx
    push %rcx
    call hex64
    pop %rax
    /* Fetch probe: dump the 8 bytes AT the entry point, then park. If
     * they match the linked ap_entry bytes, instruction fetch is proven
     * and the fault lies in C execution; otherwise the entry mapping is
     * wrong. Either way no silent reset goes undiagnosed. */
    movb $0x59,%ah
    movl $0x3FD,%edx
1:  inb %dx,%al
    testb $0x20,%al
    jz 1b
    movl $0x3F8,%edx
    movb %ah,%al
    outb %al,%dx
    movq (%rax),%rcx
    call hex64
    cli
halt_ap:
    hlt
    jmp halt_ap
/* hex64: print RCX as 16 hex digits. Clobbers RAX/RCX/R11/BL/DX;
 * preserves R10, RBX high bits and the stack. */
hex64:
    movl $16,%r11d
hex64_loop:
    rolq $4,%rcx
    movb %cl,%ah
    andb $0x0F,%ah
    addb $0x30,%ah
    cmpb $0x3A,%ah
    jb hex64_digit
    addb $0x07,%ah
hex64_digit:
    movl $0x3FD,%edx
hex64_wait:
    inb %dx,%al
    testb $0x20,%al
    jz hex64_wait
    movl $0x3F8,%edx
    movb %ah,%al
    outb %al,%dx
    decl %r11d
    jnz hex64_loop
    retq
    .globl smp_trampoline_end
smp_trampoline_end:
    .size smp_trampoline_start,.-smp_trampoline_start
    .section .note.GNU-stack,"",@progbits
