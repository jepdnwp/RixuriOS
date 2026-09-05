#include "syscall.h"
#include "../process/process.h"
#include "../sched/scheduler.h"
#include "../mm/uaccess.h"
#include "../../include/kernel.h"
#include <stdint.h>
#define RIX_ENOSYS 38
#define RIX_EINVAL 22
#define RIX_EFAULT 14
#define RIX_MAX_WRITE 4096
void syscall_dispatch(rix_syscall_frame_t *frame){
    if(!frame)return;
    int64_t result=-(int64_t)RIX_ENOSYS;
    switch(frame->rax){
        case RIX_SYS_GETPID: result=(int64_t)process_current();break;
        case RIX_SYS_WRITE:{
            uint64_t fd=frame->rdi;uint64_t src=frame->rsi;uint64_t len=frame->rdx;
            if(fd>2||len>RIX_MAX_WRITE){result=-(int64_t)RIX_EINVAL;break;}
            char buffer[RIX_MAX_WRITE];if(len&&copy_from_user(buffer,src,(size_t)len)!=0){result=-(int64_t)RIX_EFAULT;break;}
            serial_write_n(buffer,(size_t)len);result=(int64_t)len;break;
        }
        case RIX_SYS_EXIT:
            if(process_exit(process_current(),frame->rdi)!=0)result=-(int64_t)RIX_EINVAL;
            else scheduler_exit_current();
            break;
        default: break;
    }
    frame->rax=(uint64_t)result;
}
void syscall_init(void){ }
