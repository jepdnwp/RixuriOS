.section .text
.global rix_context_switch
.type rix_context_switch,@function
/* rdi = old_rsp storage, rsi = new_rsp */
rix_context_switch:
    pushq %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp,(%rdi)
    movq %rsi,%rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    retq
.size rix_context_switch,.-rix_context_switch
