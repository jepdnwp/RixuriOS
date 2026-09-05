.section .text
.global x86_enter_user
.type x86_enter_user,@function
x86_enter_user:
    movq %rdi,%cr3
    cli
    pushq $0x20
    pushq %rdx
    pushfq
    popq %rax
    orq $0x200,%rax
    pushq %rax
    pushq $0x18
    pushq %rsi
    iretq
