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

.global x86_enter_user_context
.type x86_enter_user_context,@function
x86_enter_user_context:
    movq %rdi,%cr3
    cli
    pushq $0x23
    movq 136(%rsi),%rax
    pushq %rax
    movq 128(%rsi),%rax
    orq $0x202,%rax
    andq $~0x3000,%rax
    pushq %rax
    pushq $0x1b
    movq 120(%rsi),%rax
    pushq %rax
    movq 0(%rsi),%r15
    movq 8(%rsi),%r14
    movq 16(%rsi),%r13
    movq 24(%rsi),%r12
    movq 32(%rsi),%r11
    movq 40(%rsi),%r10
    movq 48(%rsi),%r9
    movq 56(%rsi),%r8
    movq 64(%rsi),%rbp
    movq 72(%rsi),%rdi
    movq 88(%rsi),%rdx
    movq 96(%rsi),%rcx
    movq 104(%rsi),%rbx
    movq 112(%rsi),%rax
    movq 80(%rsi),%rsi
    iretq

.section .note.GNU-stack,"",@progbits
