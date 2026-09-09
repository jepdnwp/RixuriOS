.section .text
.extern current_pml4_phys
.global x86_enter_user
.type x86_enter_user,@function
x86_enter_user:
    cli
    movq %rdi,%cr3
    /* process_activate defers the user CR3 load to this trampoline.  Keep
       the software walker/uaccess view in lockstep before the first user
       instruction can issue int 0x80.  current_pml4_phys is in the shared
       low identity mapping, so this store remains valid after the CR3 load. */
    movq %rdi,current_pml4_phys(%rip)
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
    cli
    movq %rdi,%cr3
    movq %rdi,current_pml4_phys(%rip)
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
    cli
    movq %rdi,%cr3
    movq %rdi,current_pml4_phys(%rip)
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
