.section .text
/* Phase F1 (Phases 03+07): fault-safe user copies. These raw routines do
 * NO validation (the C wrapper validates first for the fast -EFAULT
 * path); they exist so a mapping that vanishes between validation and
 * access (today unreachable — no concurrent unmapper exists yet — but
 * one clone away in Phase 25) faults into the per-CPU recovery record
 * instead of freezing the kernel.
 *
 * Exactly ONE instruction per function touches user memory, bracketed
 * by fault_begin/fault_end symbols. The #PF dispatcher resumes at the
 * fixup label (returns -EFAULT) only when RIP lands inside that bracket
 * with this CPU's record armed. The kernel-side access sits OUTSIDE any
 * bracket: a fault there is a genuine kernel bug and takes the normal
 * freeze path, never the fixup.
 *
 * Contract: sysv ABI, clobbers rcx. The C wrapper disables preemption
 * across arm+call+disarm, so the task cannot migrate mid-copy and the
 * record (indexed by CPU) cannot be observed by another CPU. The fault
 * itself does not disturb the stack (frame on IST), so resuming at the
 * fixup label with RAX set is sound. */

.global uaccess_copy_from_user_raw
.type uaccess_copy_from_user_raw,@function
uaccess_copy_from_user_raw:      /* rdi=kernel dst, rsi=user src, rdx=len */
    xorl %eax,%eax
    testq %rdx,%rdx
    jz .Lfrom_done
.Lfrom_loop:
.global uaccess_from_fault_begin
uaccess_from_fault_begin:
    movb (%rsi),%cl             /* USER load: the only covered insn */
.global uaccess_from_fault_end
uaccess_from_fault_end:
    movb %cl,(%rdi)             /* kernel store: uncovered, freeze on fault */
    incq %rsi
    incq %rdi
    decq %rdx
    jnz .Lfrom_loop
.Lfrom_done:
    ret
.global uaccess_from_fixup
uaccess_from_fixup:
    movl $-14,%eax              /* -EFAULT */
    ret

.global uaccess_copy_to_user_raw
.type uaccess_copy_to_user_raw,@function
uaccess_copy_to_user_raw:        /* rdi=user dst, rsi=kernel src, rdx=len */
    xorl %eax,%eax
    testq %rdx,%rdx
    jz .Lto_done
.Lto_loop:
    movb (%rsi),%cl             /* kernel load: uncovered, freeze on fault */
.global uaccess_to_fault_begin
uaccess_to_fault_begin:
    movb %cl,(%rdi)             /* USER store: the only covered insn */
.global uaccess_to_fault_end
uaccess_to_fault_end:
    incq %rsi
    incq %rdi
    decq %rdx
    jnz .Lto_loop
.Lto_done:
    ret
.global uaccess_to_fixup
uaccess_to_fixup:
    movl $-14,%eax              /* -EFAULT */
    ret

.section .note.GNU-stack,"",@progbits
