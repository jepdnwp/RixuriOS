#include "syscall.h"
#include "../process/process.h"
#include "../process/signal.h"
#include "../process/address_space.h"
#include "../ipc/shared_memory.h"
#include "../sched/scheduler.h"
#include "../mm/uaccess.h"
#include "../vfs/vfs.h"
#include "../tty/tty.h"
#include "../../include/kernel.h"
#include <stdint.h>
#define RIX_ENOSYS 38
#define RIX_EINVAL 22
#define RIX_EFAULT 14
#define RIX_ESRCH 3
#define RIX_MAX_IO 4096
#define RIX_IO_CHUNK 256
static int user_string(uint64_t src,char *dst,size_t cap){if(!dst||cap<2)return -1;for(size_t i=0;i+1<cap;i++){uint8_t c;if(copy_from_user(&c,src+i,1)!=0)return -1;dst[i]=(char)c;if(!c)return 0;}dst[cap-1]=0;return -1;}
void syscall_dispatch(rix_syscall_frame_t*frame){
 if(!frame)return;
 int64_t result=-(int64_t)RIX_ENOSYS;pid_t self=process_current();
 switch(frame->rax){
 case RIX_SYS_READ:{uint64_t fd=frame->rdi,dst=frame->rsi,len=frame->rdx;if(len>RIX_MAX_IO){result=-RIX_EINVAL;break;}if(fd==0){uint8_t b[RIX_IO_CHUNK];size_t n=(size_t)(len>RIX_IO_CHUNK?RIX_IO_CHUNK:len),got=0;if(tty_read(0,b,n,&got)!=0){result=-RIX_EINVAL;break;}if(got&&copy_to_user(dst,b,got)!=0){result=-RIX_EFAULT;break;}result=(int64_t)got;break;}if(fd<=2){result=-RIX_EINVAL;break;}uint8_t b[RIX_IO_CHUNK];size_t done=0;while(done<len){size_t n=(size_t)(len-done);if(n>RIX_IO_CHUNK)n=RIX_IO_CHUNK;size_t got=0;if(vfs_read(self,(int)fd,b,n,&got)!=0){result=done?((int64_t)done):-(int64_t)RIX_EINVAL;break;}if(got&&copy_to_user(dst+done,b,got)!=0){result=done?((int64_t)done):-(int64_t)RIX_EFAULT;break;}done+=got;if(got<n)break;}if(done==len||len==0)result=(int64_t)done;break;}
 case RIX_SYS_WRITE:{uint64_t fd=frame->rdi,src=frame->rsi,len=frame->rdx;if(len>RIX_MAX_IO){result=-RIX_EINVAL;break;}if(fd==1||fd==2){uint8_t b[RIX_IO_CHUNK];uint64_t done=0;while(done<len){size_t n=(size_t)(len-done);if(n>RIX_IO_CHUNK)n=RIX_IO_CHUNK;if(copy_from_user(b,src+done,n)!=0){result=done?((int64_t)done):-(int64_t)RIX_EFAULT;break;}size_t wrote=0;if(tty_output(0,b,n,&wrote)!=0){result=done?((int64_t)done):-(int64_t)RIX_EINVAL;break;}serial_write_n((const char*)b,wrote);done+=wrote;}if(done==len)result=(int64_t)done;break;}if(fd==0){result=-RIX_EINVAL;break;}uint8_t b[RIX_IO_CHUNK];size_t done=0;while(done<len){size_t n=(size_t)(len-done);if(n>RIX_IO_CHUNK)n=RIX_IO_CHUNK;if(copy_from_user(b,src+done,n)!=0){result=done?((int64_t)done):-(int64_t)RIX_EFAULT;break;}size_t wrote=0;if(vfs_write(self,(int)fd,b,n,&wrote)!=0){result=done?((int64_t)done):-(int64_t)RIX_EINVAL;break;}done+=wrote;if(wrote<n)break;}if(done==len||len==0)result=(int64_t)done;break;}
 case RIX_SYS_DUP:{int new_fd;if(vfs_dup(self,(int)frame->rdi,&new_fd)!=0){result=-RIX_EINVAL;break;}result=new_fd;break;}
 case RIX_SYS_PIPE:{int fds[2];if(vfs_pipe(self,&fds[0],&fds[1])!=0){result=-RIX_EINVAL;break;}if(copy_to_user(frame->rdi,fds,sizeof(fds))!=0){(void)vfs_close(self,fds[0]);(void)vfs_close(self,fds[1]);result=-RIX_EFAULT;break;}result=0;break;}
 case RIX_SYS_OPENAT:{char path[RIX_VFS_PATH_MAX];if(user_string(frame->rsi,path,sizeof(path))!=0){result=-RIX_EFAULT;break;}int fd;if(vfs_open(self,path,(uint32_t)frame->rdx,(uint32_t)frame->r10,&fd)!=0)result=-RIX_EINVAL;else result=fd;break;}
 case RIX_SYS_CLOSE:if(vfs_close(self,(int)frame->rdi)!=0)result=-RIX_EINVAL;else result=0;break;
 case RIX_SYS_STAT:{char path[RIX_VFS_PATH_MAX];if(user_string(frame->rdi,path,sizeof(path))!=0){result=-RIX_EFAULT;break;}rix_vnode_t st;if(vfs_stat(path,&st)||copy_to_user(frame->rsi,&st,sizeof(st))!=0)result=-RIX_EINVAL;else result=0;break;}
 case RIX_SYS_EXIT:if(process_exit(self,frame->rdi)!=0)result=-RIX_EINVAL;else scheduler_exit_current();break;
 case RIX_SYS_WAIT:{pid_t wanted=(pid_t)frame->rdi;uint64_t status=0;pid_t child=0;int rc=process_wait(self,wanted,&status,&child);if(rc<0){result=-RIX_EINVAL;break;}if(rc>0){result=0;break;}if(copy_to_user(frame->rsi,&status,sizeof(status))!=0){result=-RIX_EFAULT;break;}result=(int64_t)child;break;}
 case RIX_SYS_KILL:if(process_signal_send((pid_t)frame->rdi,(unsigned)frame->rsi)!=0)result=-RIX_ESRCH;else result=0;break;
 case RIX_SYS_SIGMASK:if(process_signal_mask(self,frame->rdi)!=0)result=-RIX_EINVAL;else result=0;break;
 case RIX_SYS_SIGPENDING:{uint64_t pending;if(process_signal_pending(self,&pending)!=0||copy_to_user(frame->rdi,&pending,sizeof(pending))!=0)result=-RIX_EFAULT;else result=0;break;}
 case RIX_SYS_SHM_CREATE:{uint32_t id;if(shm_create(frame->rdi,(rix_shm_id_t*)&id)!=0){result=-RIX_EINVAL;break;}if(copy_to_user(frame->rsi,&id,sizeof(id))!=0){(void)shm_destroy(id);result=-RIX_EFAULT;break;}result=0;break;}
 case RIX_SYS_SHM_MAP:{uint32_t id=(uint32_t)frame->rdi;uint64_t va=frame->rsi,flags=frame->rdx;if(shm_map(id,self,va,flags)!=0)result=-RIX_EINVAL;else result=0;break;}
 case RIX_SYS_SHM_UNMAP:{uint32_t id=(uint32_t)frame->rdi;if(shm_unmap(id,self,frame->rsi)!=0)result=-RIX_EINVAL;else result=0;break;}
 case RIX_SYS_SHM_DESTROY:if(shm_destroy((uint32_t)frame->rdi)!=0)result=-RIX_EINVAL;else result=0;break;
 default:break;
 }
 frame->rax=(uint64_t)result;
}
void syscall_init(void){}
