.section .text
.global x86_enter_user
.type x86_enter_user,@function
x86_enter_user:
    movq %rdi,%cr3
    cli
    movw $0x23,%ax
    movw %ax,%ds
    movw %ax,%es
    pushq $0x23
    pushq %rdx
    pushfq
    popq %rax
    orq $0x202,%rax
    andq $~0x3000,%rax
    pushq %rax
    pushq $0x1b
    pushq %rsi
    iretq

.global x86_enter_user_return
.type x86_enter_user_return,@function
x86_enter_user_return:
    movq %rdi,%cr3
    cli
    movw $0x23,%ax
    movw %ax,%ds
    movw %ax,%es
    pushq $0x23
    pushq %rdx
    pushfq
    popq %rax
    orq $0x202,%rax
    andq $~0x3000,%rax
    pushq %rax
    pushq $0x1b
    pushq %rsi
    movq %rcx,%rax
    iretq

.section .note.GNU-stack,"",@progbits
