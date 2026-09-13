.section .text
.extern current_pml4_phys
.extern kernel_log
.global x86_enter_user
.type x86_enter_user,@function
x86_enter_user:
    cli
    cld
    pushq %rdi
    pushq %rsi
    pushq %rdx
    leaq .Luser_pre_cr3(%rip),%rdi
    call kernel_log
    popq %rdx
    popq %rsi
    popq %rdi

    /* IRETQ does not reload DS/ES.  Leaving the kernel
       data selector cached here makes the first CPL3 data access fault on
       real CPUs.  Load the user data selector while still at CPL0. */
    movw $0x23,%ax
    movw %ax,%ds
    movw %ax,%es

    pushq %rdi
    pushq %rsi
    pushq %rdx
    leaq .Luser_frame_ready(%rip),%rdi
    call kernel_log
    popq %rdx
    popq %rsi
    popq %rdi

    /* Build the IRETQ frame on the current task stack, not in a shared
       32-byte .bss slot.  The task stack lives in kernel .bss (PML4[0]
       identity, shared into every user address space), so it stays mapped
       across MOV CR3, while a tiny .bss frame overflows on any NMI in the
       MOV CR3 -> far-return window (real ASUS/AMD boards deliver SMIs/NMIs
       where QEMU delivers none).  IRETQ (not LRETQ) loads a controlled
       RFLAGS.  IF stays CLEAR here by design: a pending physical IRQ
       (PIT/keyboard, certain on real HW) must not fire before the first
       synchronous int 0x80, otherwise it races TSS.RSP0/IRETQ delivery.
       The first syscall return sets IF (see syscall_dispatch). */
    pushq $0x23
    pushq %rdx
    pushq $0x002
    pushq $0x1b
    pushq %rsi
    movq %rdi,%cr3
    /* .bss is in shared PML4[0]; safe after CR3 while IF=0.  Sync the SW
       tracker immediately so a fault on the first user instruction walks
       the target tables instead of the stale kernel root. */
    movq %rdi,current_pml4_phys(%rip)
    /* C marker: CR3 is loaded and the tracker is in sync; only iretq
       remains. The framebuffer is mapped under every CR3, so this line is
       safe to emit here. If C never appears, the freeze is inside the
       mov-cr3/store above; if C appears with nothing after, iretq or the
       first user instruction is at fault (watch for FAULT: lines). */
    pushq %rdi
    leaq .Luser_post_cr3(%rip),%rdi
    call kernel_log
    popq %rdi
    iretq

.section .rodata
.Luser_pre_cr3:
    .asciz "BOOT: USER ENTRY A pre-cr3\r\n"
.Luser_frame_ready:
    .asciz "BOOT: USER ENTRY B iret-frame-ready\r\n"
.Luser_post_cr3:
    .asciz "BOOT: USER ENTRY C post-cr3\r\n"

/* NOTE: code below must stay in .text. The .rodata section above is only
 * for the log strings; entry trampolines execute with NX enforced on
 * .rodata once kernel section permissions apply, so they must never
 * fall through into a data section. */
.text
.global x86_enter_user_return
.type x86_enter_user_return,@function
x86_enter_user_return:
    cli
    cld
    movq %rdi,%cr3
    movq %rdi,current_pml4_phys(%rip)
    movw $0x23,%ax
    movw %ax,%ds
    movw %ax,%es
    pushq $0x23
    pushq %rdx
    pushfq
    popq %rax
    orq $0x002,%rax
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
    cld
    movq %rdi,%cr3
    movq %rdi,current_pml4_phys(%rip)
    pushq $0x23
    movq 136(%rsi),%rax
    pushq %rax
    movq 128(%rsi),%rax
    andq $~0x200,%rax
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
