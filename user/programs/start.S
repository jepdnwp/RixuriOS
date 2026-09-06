.section .text
.global _start
.type _start,@function
_start:
    movq (%rsp),%rdi
    leaq 8(%rsp),%rsi
    leaq 16(%rsp,%rdi,8),%rdx
    call program_main
    movl %eax,%edi
    movl $60,%eax
    int $0x80
1:
    hlt
    jmp 1b
.size _start,.-_start
.section .note.GNU-stack,"",@progbits
