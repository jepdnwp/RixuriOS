#pragma once
#include <stddef.h>
#include <stdint.h>

/* Phase F1: uaccess fault recovery. The raw copies in
 * kernel/arch/x86_64/uaccess.S bracket exactly one user-memory
 * instruction each; a kernel #PF whose RIP lands inside a bracket with
 * this CPU's record armed resumes at that direction's fixup label
 * (returning -EFAULT) instead of freezing. */
extern uint8_t uaccess_from_fault_begin[];
extern uint8_t uaccess_from_fault_end[];
extern uint8_t uaccess_from_fixup[];
extern uint8_t uaccess_to_fault_begin[];
extern uint8_t uaccess_to_fault_end[];
extern uint8_t uaccess_to_fixup[];
int uaccess_copy_from_user_raw(void *kernel_dst, uint64_t user_src, size_t length);
int uaccess_copy_to_user_raw(uint64_t user_dst, const void *kernel_src, size_t length);
struct x86_fault_frame;
/* #PF dispatch hook (idt.c): resume armed uaccess faults at their fixup
 * label. 1 consumed, 0 fall through to forensics/freeze. */
int uaccess_fixup_frame(struct x86_fault_frame *frame);

int user_range_valid(uint64_t address,size_t length,int write);
int copy_from_user(void *kernel_dst,uint64_t user_src,size_t length);
int copy_to_user(uint64_t user_dst,const void *kernel_src,size_t length);
/* Boot self-test: faults a known-unmapped user VA through the ARMED raw
 * path (validation bypassed on purpose) and requires the fixup to
 * convert it to -EFAULT. Proves the #PF-to-fixup-to-resume chain end to
 * end. Runs once after IDT+VMM+scheduler are live. 0 ok, -1 no probe. */
int uaccess_fixup_selftest(void);
