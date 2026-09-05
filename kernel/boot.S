.section .text.boot
.global _start
.type _start,@function

_start:
    cli
    mov %rdi, %rdi
    lea stack_top(%rip), %rsp
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
