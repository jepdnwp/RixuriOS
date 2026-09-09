.section .text
.global load_cr3_raw
.type load_cr3_raw,@function
/* Minimal CR3 loader: exactly one architectural transition (mov to CR3,
 * which serializes and flushes the TLB when PCIDE=0) plus ret. First
 * argument (%rdi, SysV) is the physical PML4 base, low 12 bits must be
 * zero (PCID unused). Caller validates, syncs kernel mappings, prints
 * before/after markers, and syncs the software CR3 tracker. */
load_cr3_raw:
    movq %rdi,%cr3
    ret

.section .note.GNU-stack,"",@progbits
