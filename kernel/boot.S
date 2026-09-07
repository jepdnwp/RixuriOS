.section .text.boot
.global _start
.type _start,@function

_start:
    cli
    /*
     * UEFI passes the handoff in RDI, but the object lives in
     * EFI_LOADER_DATA.  After ExitBootServices that storage is no longer
     * a safe long-term home for kernel metadata.  Copy the fixed-size
     * handoff before changing stacks; kernel_main then only dereferences
     * the copy on our own kernel .bss-backed stack.
     */
    mov %rdi, %rsi
    lea stack_top(%rip), %rsp
    sub $128, %rsp
    mov %rsp, %rdi
    mov $13, %rcx                 /* sizeof(rixuri_boot_info_t) / 8 */
    cld
    rep movsq
    mov %rsp, %rdi
    and $-16, %rsp
    call kernel_main

1:
    hlt
    jmp 1b

.section .bss
.align 16
stack_bottom:
.skip 32768
stack_top:

.section .note.GNU-stack,"",@progbits
