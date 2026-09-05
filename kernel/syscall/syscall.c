#include "syscall.h"
/* SYSCALL/SYSRET is intentionally not enabled until user GDT/TSS/GS-base
 * and the complete trap ABI are installed.  Enabling a partial entry path
 * would permit an invalid return frame and is therefore a boot-time hazard. */
void syscall_init(void) {}
