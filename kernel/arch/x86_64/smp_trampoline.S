#include "smp.h"

    .section .text
    .code16
    .globl smp_trampoline_start
    .type smp_trampoline_start,@function
/* AP real-mode entry: CS = SIPI vector, IP = 0, so CS.base == page base.
 * DS is pointed at CS; every data access below is page-relative and the
 * template stays position-independent except the one patched long jump. */
smp_trampoline_start:
    cli
    movw %cs,%ax
    movw %ax,%ds
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
    movw $SMP_TRAMP_SEL_DATA64,%ax
    movw %ax,%ds
    movw %ax,%es
    movw %ax,%ss
    /* FS.base (page) survives the mode switch: data via %fs, stack, entry. */
    movq %fs:SMP_TRAMP_DATA_STACK,%rsp
    jmp *%fs:SMP_TRAMP_DATA_ENTRY
    .globl smp_trampoline_end
smp_trampoline_end:
    .size smp_trampoline_start,.-smp_trampoline_start
    .section .note.GNU-stack,"",@progbits
