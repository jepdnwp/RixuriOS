#include "syscall.h"
#include "../process/process.h"
#include <stdint.h>

#define RIX_ENOSYS 38
#define RIX_EINVAL 22

void syscall_dispatch(rix_syscall_frame_t *frame){
    if(!frame)return;
    int64_t result=-(int64_t)RIX_ENOSYS;
    switch(frame->rax){
        case RIX_SYS_GETPID: result=(int64_t)process_current();break;
        case RIX_SYS_EXIT:
            result=process_exit(process_current(),frame->rdi)==0?0:-(int64_t)RIX_EINVAL;
            break;
        default: break;
    }
    frame->rax=(uint64_t)result;
}

void syscall_init(void){
    /* Vector 0x80 is installed by the IDT because it needs DPL=3. */
}
