#include "syscall.h"
#include "../process/process.h"
#include "../process/signal.h"
#include "../sched/scheduler.h"
#include "../mm/uaccess.h"
#include "../../include/kernel.h"
#include <stdint.h>
#define RIX_ENOSYS 38
#define RIX_EINVAL 22
#define RIX_EFAULT 14
#define RIX_ESRCH 3
#define RIX_MAX_WRITE 4096
#define RIX_WRITE_CHUNK 256
void syscall_dispatch(rix_syscall_frame_t*frame){
 if(!frame)return;int64_t result=-(int64_t)RIX_ENOSYS;pid_t self=process_current();
 switch(frame->rax){
 case RIX_SYS_GETPID:result=(int64_t)self;break;
 case RIX_SYS_WRITE:{uint64_t fd=frame->rdi,src=frame->rsi,len=frame->rdx;if(fd>2||len>RIX_MAX_WRITE){result=-RIX_EINVAL;break;}uint8_t b[RIX_WRITE_CHUNK];uint64_t done=0;while(done<len){size_t n=(size_t)(len-done);if(n>RIX_WRITE_CHUNK)n=RIX_WRITE_CHUNK;if(copy_from_user(b,src+done,n)!=0){result=-RIX_EFAULT;break;}serial_write_n((const char*)b,n);done+=n;}if(done==len)result=(int64_t)done;break;}
 case RIX_SYS_EXIT:if(process_exit(self,frame->rdi)!=0)result=-RIX_EINVAL;else scheduler_exit_current();break;
 case RIX_SYS_WAIT:{pid_t wanted=(pid_t)frame->rdi;uint64_t status=0;pid_t child=0;int rc=process_wait(self,wanted,&status,&child);if(rc<0){result=-RIX_EINVAL;break;}if(rc>0){result=0;break;}if(copy_to_user(frame->rdx,&status,sizeof(status))!=0){result=-RIX_EFAULT;break;}result=(int64_t)child;break;}
 case RIX_SYS_KILL:{if(process_signal_send((pid_t)frame->rdi,(unsigned)frame->rsi)!=0)result=-RIX_ESRCH;else result=0;break;}
 case RIX_SYS_SIGMASK:{if(process_signal_mask(self,frame->rdi)!=0)result=-RIX_EINVAL;else result=0;break;}
 case RIX_SYS_SIGPENDING:{uint64_t pending;if(process_signal_pending(self,&pending)!=0||copy_to_user(frame->rdi,&pending,sizeof(pending))!=0)result=-RIX_EFAULT;else result=0;break;}
 default:break;
 }
 frame->rax=(uint64_t)result;
}
void syscall_init(void){}
