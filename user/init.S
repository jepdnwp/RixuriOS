.section .text
.global _start
.type _start,@function
_start:
    mov $1, %rax
    mov $1, %rdi
    lea message(%rip), %rsi
    mov $(message_end-message), %rdx
    int $0x80

    mov $60, %rax
    xor %rdi, %rdi
    int $0x80
1:
    hlt
    jmp 1b

.section .rodata
.align 1
message:
    .ascii "RixuriOS ring3 userspace online\r\n"
message_end:
