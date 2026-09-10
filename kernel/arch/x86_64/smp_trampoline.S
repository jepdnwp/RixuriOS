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
    movw $4096,%cx
1:  inb %dx,%al
    testb $0x20,%al
    jnz 2f
    decw %cx
    jnz 1b
2:  movw $0x3F8,%dx
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
    movl $4096,%ecx
1:  inb %dx,%al
    testb $0x20,%al
    jnz 2f
    decl %ecx
    jnz 1b
2:  movl $0x3F8,%edx
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
    movl $4096,%r12d
1:  inb %dx,%al
    testb $0x20,%al
    jnz 2f
    decl %r12d
    jnz 1b
2:  movl $0x3F8,%edx
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
    /* The trampoline GDT is already flat and valid in long mode. Do not load
     * firmware/BSP descriptor snapshots here: a malformed or incompatible
     * firmware GDT can fault before the AP publishes its state. APs keep IF
     * clear and remain parked in ap_entry until per-CPU setup exists. */
    /* Publish ONLINE without entering C or relying on the AP's TSS. */
    movq SMP_TRAMP_DATA_ONLINE(%r10),%r14
    movl $3,(%r14)
    mfence
    /* The AP is now in the BSP's descriptor environment and has published
     * ONLINE.  Jump directly to the C entry pointer.  Never dereference the
     * function pointer as data: that old diagnostic probe caused a page fault
     * on physical APs and left the BSP with a partial online count. */
    movq SMP_TRAMP_DATA_ENTRY(%r10),%rax
    jmp *%rax
/* hex64: print RCX as 16 hex digits. Clobbers RAX/RCX/R11/R12D/BL/DX;
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
    movl $4096,%r12d
hex64_wait:
    inb %dx,%al
    testb $0x20,%al
    jnz hex64_done
    decl %r12d
    jnz hex64_wait
hex64_done:
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
