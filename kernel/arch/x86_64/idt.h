#pragma once
#include <stdint.h>

/* Fault frame as built by interrupts.S (RSP+120 at dispatch): the C
 * handler receives a pointer to the LIVE on-stack frame, so rewriting
 * frame->rip resumes at the new address (uaccess fixup, user-kill
 * paths). Layout must match the SAVE_REGS + vector/error pushes. */
struct x86_fault_frame {
    uint64_t vector, error, rip, cs, rflags, rsp, ss;
};

void idt_init(void);
void idt_enable(void);
void idt_disable(void);
