.section .text.boot
.global _start
.type _start,@function

_start:
    cli
    /* RDI = original boot_info ptr (may be in UEFI-decommitted memory).
     * Copy 104-byte struct to stack (kernel BSS, guaranteed mapped). */
    mov %rdi, %rsi              /* RSI = src = original boot_info */
    lea stack_top(%rip), %rsp
    sub $128, %rsp
    mov %rsp, %rdi              /* RDI = dst = stack buffer */
    mov $13, %rcx               /* 104 bytes / 8 = 13 qwords */
    cld
    rep movsq
    mov %rsp, %rdi              /* RDI = new boot_info for kernel_main */

    /* Early diagnostic: red pixel at (0,0) */
    mov 0x50(%rdi), %rax
    test %rax, %rax
    jz 1f
    movl $0xFF0000FF, (%rax)
1:
    and $-16, %rsp
    call kernel_main

2:
    hlt
    jmp 2b

.section .bss
.align 16
stack_bottom:
.skip 32768
stack_top:

.section .note.GNU-stack,"",@progbits
