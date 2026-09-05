.section .text
.extern x86_exception_dispatch
.extern x86_irq_dispatch
.extern syscall_dispatch

.macro SAVE_REGS
    pushq %rax; pushq %rbx; pushq %rcx; pushq %rdx; pushq %rsi; pushq %rdi; pushq %rbp
    pushq %r8; pushq %r9; pushq %r10; pushq %r11; pushq %r12; pushq %r13; pushq %r14; pushq %r15
.endm
.macro RESTORE_REGS
    popq %r15; popq %r14; popq %r13; popq %r12; popq %r11; popq %r10; popq %r9; popq %r8
    popq %rbp; popq %rdi; popq %rsi; popq %rdx; popq %rcx; popq %rbx; popq %rax
.endm
.macro ISR_NOERR n
.global isr\n
.type isr\n,@function
isr\n:
    pushq $0
    pushq $\n
    SAVE_REGS
    leaq 120(%rsp), %rdi
    call x86_exception_dispatch
    RESTORE_REGS
    addq $16, %rsp
    iretq
.endm
.macro ISR_ERR n
.global isr\n
.type isr\n,@function
isr\n:
    pushq $\n
    SAVE_REGS
    leaq 120(%rsp), %rdi
    call x86_exception_dispatch
    RESTORE_REGS
    addq $8, %rsp
    iretq
.endm
.macro IRQ n
.global isr\n
.type isr\n,@function
isr\n:
    pushq $0
    pushq $\n
    SAVE_REGS
    leaq 120(%rsp), %rdi
    call x86_irq_dispatch
    RESTORE_REGS
    addq $16, %rsp
    iretq
.endm

.global isr_default
.type isr_default,@function
isr_default:
    pushq $0
    pushq $255
    SAVE_REGS
    leaq 120(%rsp), %rdi
    call x86_exception_dispatch
    RESTORE_REGS
    addq $16, %rsp
    iretq

.global isr128
.type isr128,@function
isr128:
    pushq $0
    pushq $128
    SAVE_REGS
    movq %rsp,%rdi
    call syscall_dispatch
    RESTORE_REGS
    addq $16,%rsp
    iretq

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR 8
ISR_NOERR 9
ISR_ERR 10
ISR_ERR 11
ISR_ERR 12
ISR_ERR 13
ISR_ERR 14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR 17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_ERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_ERR 29
ISR_ERR 30
ISR_NOERR 31
IRQ 32
IRQ 33
IRQ 34
IRQ 35
IRQ 36
IRQ 37
IRQ 38
IRQ 39
IRQ 40
IRQ 41
IRQ 42
IRQ 43
IRQ 44
IRQ 45
IRQ 46
IRQ 47
