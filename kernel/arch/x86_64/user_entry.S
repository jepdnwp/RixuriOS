.section .text
.global x86_enter_user
.type x86_enter_user,@function
x86_enter_user:
    movq %rdi,%cr3
    cli
    movw $0x20,%ax
    movw %ax,%ds
    movw %ax,%es
    movw %ax,%ss
    pushq $0x20
    pushq %rdx
    pushfq
    popq %rax
    orq $0x202,%rax
    andq $~0x3000,%rax
    pushq %rax
    pushq $0x18
    pushq %rsi
    iretq
