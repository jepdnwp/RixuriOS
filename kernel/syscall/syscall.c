#include "syscall.h"
#include "../process/process.h"
#include "../process/signal.h"
#include "../process/address_space.h"
#include "../ipc/shared_memory.h"
#include "../sched/scheduler.h"
#include "../mm/uaccess.h"
#include "../mm/heap.h"
#include "../vfs/vfs.h"
#include "../tty/tty.h"
#include "../time/time.h"
#include "../../include/kernel.h"
#include <stdint.h>
#define RIX_ENOSYS 38
#define RIX_EINVAL 22
#define RIX_EACCES 13
#define RIX_EEXIST 17
#define RIX_EFAULT 14
#define RIX_EINTR 4
#define RIX_ESRCH 3
#define RIX_MAX_IO 4096
#define RIX_IO_CHUNK 256
#define RIX_MAX_EXEC_IMAGE 131072u
#define RIX_SYS_WAITPID 247
#define RIX_WAITPID_NOHANG 1u
static uint8_t exec_image_buffers[RIX_PROCESS_MAX][RIX_MAX_EXEC_IMAGE];
static int user_string(uint64_t src,char *dst,size_t cap){if(!dst||cap<2)return -1;for(size_t i=0;i+1<cap;i++){uint8_t c;if(copy_from_user(&c,src+i,1)!=0)return -1;dst[i]=(char)c;if(!c)return 0;}dst[cap-1]=0;return -1;}
static int copy_string_vector(uint64_t vector,char storage[][RIX_PROCESS_ARG_TEXT_MAX],const char *pointers[],size_t *count){if(!count)return -1;*count=0;if(!vector)return 0;for(size_t i=0;i<RIX_PROCESS_ARG_MAX;i++){uint64_t user_ptr=0;if(copy_from_user(&user_ptr,vector+i*sizeof(user_ptr),sizeof(user_ptr))!=0)return -1;if(!user_ptr){*count=i;return 0;}if(user_string(user_ptr,storage[i],RIX_PROCESS_ARG_TEXT_MAX)!=0)return -1;pointers[i]=storage[i];}return -1;}
static int syscall_interrupted(pid_t pid){unsigned signal=0;return process_signal_take(pid,&signal)==0;}
static int vfs_result(int rc){return rc==0?0:(rc==RIX_VFS_ERR_PERMISSION?-RIX_EACCES:(rc==RIX_VFS_ERR_EXISTS?-RIX_EEXIST:-RIX_EINVAL));}
void syscall_dispatch(rix_syscall_frame_t*frame){
 if(!frame)return;
 int64_t result=-(int64_t)RIX_ENOSYS;pid_t self=process_current();
 switch(frame->rax){
 case RIX_SYS_READ:{
  uint64_t fd=frame->rdi,dst=frame->rsi,len=frame->rdx;
  if(len>RIX_MAX_IO){result=-RIX_EINVAL;break;}
  if(!len){result=0;break;}
  if(fd==0&&!vfs_fd_is_open(self,(int)fd)){
   uint8_t b[RIX_IO_CHUNK];size_t n=(size_t)(len>RIX_IO_CHUNK?RIX_IO_CHUNK:len),got=0;
   for(;;){int rc=tty_read(0,b,n,&got);if(rc==0)break;if(rc==-3){if(syscall_interrupted(self)){result=-RIX_EINTR;break;}scheduler_yield();continue;}result=-RIX_EINVAL;break;}
   if(result==-RIX_EINVAL)break;
   if(got&&copy_to_user(dst,b,got)!=0){result=-RIX_EFAULT;break;}
   result=(int64_t)got;break;
  }
  if(fd<=2&&!vfs_fd_is_open(self,(int)fd)){result=-RIX_EINVAL;break;}
  uint8_t b[RIX_IO_CHUNK];size_t done=0;
  while(done<len){
   size_t n=(size_t)(len-done);if(n>RIX_IO_CHUNK)n=RIX_IO_CHUNK;size_t got=0;
   int rc=vfs_read(self,(int)fd,b,n,&got);
   if(rc==-3&&got==0){if(syscall_interrupted(self)){result=done?((int64_t)done):-(int64_t)RIX_EINTR;break;}scheduler_yield();continue;}
   if(rc==-2&&got==0){result=(int64_t)done;break;}
   if(rc!=0&&!(rc==-3&&got)){result=done?((int64_t)done):-(int64_t)RIX_EINVAL;break;}
   if(got&&copy_to_user(dst+done,b,got)!=0){result=done?((int64_t)done):-(int64_t)RIX_EFAULT;break;}
   done+=got;if(got<n){result=(int64_t)done;break;}
  }
  if(done==len||len==0||done>0)result=(int64_t)done;
  break;
 }
 case RIX_SYS_WRITE:{uint64_t fd=frame->rdi,src=frame->rsi,len=frame->rdx;if(len>RIX_MAX_IO){result=-RIX_EINVAL;break;}if((fd==1||fd==2)&&!vfs_fd_is_open(self,(int)fd)){uint8_t b[RIX_IO_CHUNK];uint64_t done=0;while(done<len){size_t n=(size_t)(len-done);if(n>RIX_IO_CHUNK)n=RIX_IO_CHUNK;if(copy_from_user(b,src+done,n)!=0){result=done?((int64_t)done):-(int64_t)RIX_EFAULT;break;}size_t wrote=0;if(tty_output(0,b,n,&wrote)!=0){result=done?((int64_t)done):-(int64_t)RIX_EINVAL;break;}serial_write_n((const char*)b,wrote);done+=wrote;}if(done==len)result=(int64_t)done;break;}if(fd==0&&!vfs_fd_is_open(self,(int)fd)){result=-RIX_EINVAL;break;}uint8_t b[RIX_IO_CHUNK];size_t done=0;while(done<len){size_t n=(size_t)(len-done);if(n>RIX_IO_CHUNK)n=RIX_IO_CHUNK;if(copy_from_user(b,src+done,n)!=0){result=done?((int64_t)done):-(int64_t)RIX_EFAULT;break;}size_t wrote=0;if(vfs_write(self,(int)fd,b,n,&wrote)!=0){result=done?((int64_t)done):-(int64_t)RIX_EINVAL;break;}done+=wrote;if(wrote<n)break;}if(done==len||len==0)result=(int64_t)done;break;}
 case RIX_SYS_DUP:{int new_fd;if(vfs_dup(self,(int)frame->rdi,&new_fd)!=0){result=-RIX_EINVAL;break;}result=new_fd;break;}
 case RIX_SYS_DUP2:if(vfs_dup_to(self,(int)frame->rdi,(int)frame->rsi)!=0)result=-RIX_EINVAL;else result=frame->rsi;break;
 case RIX_SYS_FORK:{pid_t child;if(process_fork(self,frame->rip,frame->rsp,&child)!=0){result=-RIX_EINVAL;break;}rix_user_context_t context={frame->r15,frame->r14,frame->r13,frame->r12,frame->r11,frame->r10,frame->r9,frame->r8,frame->rbp,frame->rdi,frame->rsi,frame->rdx,frame->rcx,frame->rbx,frame->rax,frame->rip,frame->rflags,frame->rsp};rix_task_id_t task;if(scheduler_create_fork_child_context(child,&context,&task)!=0){(void)process_exit(child,127);result=-RIX_EINVAL;break;}result=(int64_t)child;break;}
 case RIX_SYS_EXECVE:{char path[RIX_VFS_PATH_MAX];char argv_storage[RIX_PROCESS_ARG_MAX][RIX_PROCESS_ARG_TEXT_MAX];char env_storage[RIX_PROCESS_ARG_MAX][RIX_PROCESS_ARG_TEXT_MAX];const char *argv[RIX_PROCESS_ARG_MAX];const char *envp[RIX_PROCESS_ARG_MAX];size_t argc=0,envc=0;if(user_string(frame->rdi,path,sizeof(path))!=0||copy_string_vector(frame->rsi,argv_storage,argv,&argc)!=0||copy_string_vector(frame->rdx,env_storage,envp,&envc)!=0){result=-RIX_EFAULT;break;}rix_vnode_t exec_node;int node_rc=vfs_stat(path,&exec_node);if(node_rc!=0){result=vfs_result(node_rc);break;}int setuid_bit=(exec_node.mode&04000u)!=0,setgid_bit=(exec_node.mode&02000u)!=0;int fd;int open_rc=vfs_open(self,path,0,0,&fd);if(open_rc!=0){result=vfs_result(open_rc);break;}void *image=exec_image_buffers[self];size_t image_size=0;if(vfs_read(self,fd,image,RIX_MAX_EXEC_IMAGE,&image_size)!=0){(void)vfs_close(self,fd);result=-RIX_EFAULT;break;}(void)vfs_close(self,fd);uint64_t entry,stack;if(process_exec_user_with_args(self,image,image_size,argv,argc,setuid_bit||setgid_bit?NULL:envp,setuid_bit||setgid_bit?0u:envc,&entry,&stack)!=0){result=-RIX_EINVAL;break;}if(process_apply_exec_credentials(self,exec_node.uid,exec_node.gid,setuid_bit,setgid_bit)!=0||process_activate(self)!=0){result=-RIX_EINVAL;break;}frame->rip=entry;frame->rsp=stack;result=0;break;}
 case RIX_SYS_SPAWN:{char name[RIX_PROCESS_NAME_MAX];uint64_t image_ptr=frame->rsi,image_size=frame->rdx;if(user_string(frame->rdi,name,sizeof(name))!=0||image_size==0||image_size>RIX_MAX_EXEC_IMAGE){result=-RIX_EINVAL;break;}void *image=kmalloc((size_t)image_size,16u);if(!image||copy_from_user(image,image_ptr,(size_t)image_size)!=0){if(image)kfree(image);result=-RIX_EFAULT;break;}pid_t child;uint64_t entry,stack;if(process_create_user(name,self,image,image_size,&child,&entry,&stack)!=0){kfree(image);result=-RIX_EINVAL;break;}kfree(image);rix_task_id_t task;if(scheduler_create_user_process(child,entry,stack,&task)!=0){(void)process_exit(child,127);result=-RIX_EINVAL;break;}result=(int64_t)child;break;}
 case RIX_SYS_PIPE:{int fds[2];if(vfs_pipe(self,&fds[0],&fds[1])!=0){result=-RIX_EINVAL;break;}if(copy_to_user(frame->rdi,fds,sizeof(fds))!=0){(void)vfs_close(self,fds[0]);(void)vfs_close(self,fds[1]);result=-RIX_EFAULT;break;}result=0;break;}
 case RIX_SYS_OPENAT:{char path[RIX_VFS_PATH_MAX];if(user_string(frame->rsi,path,sizeof(path))!=0){result=-RIX_EFAULT;break;}int fd;int rc=vfs_open(self,path,(uint32_t)frame->rdx,(uint32_t)frame->r10,&fd);if(rc!=0)result=vfs_result(rc);else result=fd;break;}
 case RIX_SYS_MKDIR:{char path[RIX_VFS_PATH_MAX];if(user_string(frame->rdi,path,sizeof(path))!=0){result=-RIX_EFAULT;break;}result=vfs_result(vfs_mkdir(path,(uint32_t)frame->rsi,process_uid(self),process_gid(self)));break;}
 case RIX_SYS_RMDIR:{char path[RIX_VFS_PATH_MAX];if(user_string(frame->rdi,path,sizeof(path))!=0){result=-RIX_EFAULT;break;}result=vfs_result(vfs_rmdir(path));break;}
 case RIX_SYS_UNLINK:{char path[RIX_VFS_PATH_MAX];if(user_string(frame->rdi,path,sizeof(path))!=0){result=-RIX_EFAULT;break;}result=vfs_result(vfs_unlink(path));break;}
 case RIX_SYS_LINK:{char old_path[RIX_VFS_PATH_MAX],new_path[RIX_VFS_PATH_MAX];if(user_string(frame->rdi,old_path,sizeof(old_path))!=0||user_string(frame->rsi,new_path,sizeof(new_path))!=0){result=-RIX_EFAULT;break;}result=vfs_result(vfs_link(old_path,new_path));break;}
 case RIX_SYS_GETDENTS:{if(frame->rdx==0||frame->rdx>16u){result=-RIX_EINVAL;break;}uint64_t offset=0;size_t count=0;for(size_t i=0;i<(size_t)frame->rdx;i++){rix_vfs_dirent_t entry;char name[RIX_VFS_NAME_MAX+1];int rc=vfs_readdir(self,(int)frame->rdi,&offset,&entry,name,sizeof(name));if(rc!=0)break;struct {uint64_t inode;uint8_t type;char name[RIX_VFS_NAME_MAX+1];} user_entry={entry.inode,entry.type,{0}};size_t n=0;while(n<RIX_VFS_NAME_MAX&&name[n]){user_entry.name[n]=name[n];++n;}if(copy_to_user(frame->rsi+count*sizeof(user_entry),&user_entry,sizeof(user_entry))!=0){result=-RIX_EFAULT;break;}++count;}if(result!=-RIX_EFAULT){if(copy_to_user(frame->r10,&count,sizeof(count))!=0)result=-RIX_EFAULT;else result=(int64_t)count;}break;}
 case RIX_SYS_CLOSE:if(vfs_close(self,(int)frame->rdi)!=0)result=-RIX_EINVAL;else result=0;break;
 case RIX_SYS_CLOSE_PIPES_EXCEPT:if(vfs_close_pipes_except(self,(int)frame->rdi,(int)frame->rsi)!=0)result=-RIX_EINVAL;else result=0;break;
 case RIX_SYS_STAT:{char path[RIX_VFS_PATH_MAX];if(user_string(frame->rdi,path,sizeof(path))!=0){result=-RIX_EFAULT;break;}rix_vnode_t st;int rc=vfs_stat(path,&st);if(rc!=0)result=vfs_result(rc);else if(copy_to_user(frame->rsi,&st,sizeof(st))!=0)result=-RIX_EFAULT;else result=0;break;}
 case RIX_SYS_GETACL:{char path[RIX_VFS_PATH_MAX];rixfs_acl_t acl;if(user_string(frame->rdi,path,sizeof(path))!=0){result=-RIX_EFAULT;break;}int rc=vfs_get_acl(path,&acl);if(rc!=0)result=vfs_result(rc);else if(copy_to_user(frame->rsi,&acl,sizeof(acl))!=0)result=-RIX_EFAULT;else result=0;break;}
 case RIX_SYS_SETACL:{char path[RIX_VFS_PATH_MAX];rixfs_acl_t acl;if(user_string(frame->rdi,path,sizeof(path))!=0||copy_from_user(&acl,frame->rsi,sizeof(acl))!=0){result=-RIX_EFAULT;break;}result=vfs_result(vfs_set_acl(path,&acl));break;}
 case RIX_SYS_CLEARACL:{char path[RIX_VFS_PATH_MAX];if(user_string(frame->rdi,path,sizeof(path))!=0){result=-RIX_EFAULT;break;}result=vfs_result(vfs_clear_acl(path));break;}
 case RIX_SYS_CHMOD:{char path[RIX_VFS_PATH_MAX];if(user_string(frame->rdi,path,sizeof(path))!=0){result=-RIX_EFAULT;break;}result=vfs_result(vfs_chmod(path,(uint32_t)frame->rsi));break;}
 case RIX_SYS_CHOWN:{char path[RIX_VFS_PATH_MAX];if(user_string(frame->rdi,path,sizeof(path))!=0){result=-RIX_EFAULT;break;}result=vfs_result(vfs_chown(path,(uint32_t)frame->rsi,(uint32_t)frame->rdx));break;}
 case RIX_SYS_RENAME:{char old_path[RIX_VFS_PATH_MAX],new_path[RIX_VFS_PATH_MAX];if(user_string(frame->rdi,old_path,sizeof(old_path))!=0||user_string(frame->rsi,new_path,sizeof(new_path))!=0){result=-RIX_EFAULT;break;}result=vfs_result(vfs_rename(old_path,new_path));break;}
 case RIX_SYS_EXIT:if(process_exit(self,frame->rdi)!=0)result=-RIX_EINVAL;else scheduler_exit_current();break;
 case RIX_SYS_WAIT:{
  pid_t wanted=(pid_t)frame->rdi;uint64_t status=0;pid_t child=0;int rc;
  for(;;){rc=process_wait(self,wanted,&status,&child);if(rc==1){if(syscall_interrupted(self)){result=-RIX_EINTR;break;}scheduler_yield();continue;}break;}
  if(result==-(int64_t)RIX_EINTR)break;
  if(rc==2||rc<0){result=-RIX_EINVAL;break;}
  if(copy_to_user(frame->rsi,&status,sizeof(status))!=0){result=-RIX_EFAULT;break;}
  result=(int64_t)child;break;
 }
 case RIX_SYS_WAITPID:{
  pid_t wanted=(pid_t)frame->rdi;uint64_t status=0;pid_t child=0;int rc=process_wait(self,wanted,&status,&child);
  if(rc==1){if((uint32_t)frame->rdx&RIX_WAITPID_NOHANG){result=0;break;}for(;;){if(syscall_interrupted(self)){result=-RIX_EINTR;break;}scheduler_yield();rc=process_wait(self,wanted,&status,&child);if(rc!=1)break;}if(result==-(int64_t)RIX_EINTR)break;}
  if(rc==2||rc<0){result=-RIX_EINVAL;break;}
  if(copy_to_user(frame->rsi,&status,sizeof(status))!=0){result=-RIX_EFAULT;break;}
  result=(int64_t)child;break;
 }
 case RIX_SYS_KILL:{pid_t target=(pid_t)frame->rdi;if(process_uid(target)!=process_uid(self)&&!process_has_capability(self,RIX_CAP_KILL)){result=-RIX_EACCES;break;}if(process_signal_send(target,(unsigned)frame->rsi)!=0)result=-RIX_ESRCH;else result=0;break;}
 case RIX_SYS_GETPID:result=(int64_t)self;break;
 case RIX_SYS_SIGMASK:if(process_signal_mask(self,frame->rdi)!=0)result=-RIX_EINVAL;else result=0;break;
 case RIX_SYS_SIGPENDING:{uint64_t pending;if(process_signal_pending(self,&pending)!=0||copy_to_user(frame->rdi,&pending,sizeof(pending))!=0)result=-RIX_EFAULT;else result=0;break;}
 case RIX_SYS_CHDIR:{char input[RIX_VFS_PATH_MAX],resolved[RIX_VFS_PATH_MAX];rix_vfs_path_t path;int rc=0;if(user_string(frame->rdi,input,sizeof(input))!=0){result=-RIX_EFAULT;break;}if(vfs_normalize_path(input,resolved,sizeof(resolved))!=0||(rc=vfs_lookup(resolved,&path))!=0||!path.node||path.node->type!=RIX_VFS_DIR||process_setcwd(self,resolved)!=0)result=rc==RIX_VFS_ERR_PERMISSION?-RIX_EACCES:-RIX_EINVAL;else result=0;break;}
 case RIX_SYS_GETCWD:{char cwd[RIX_PROCESS_CWD_MAX];size_t cap=(size_t)frame->rsi;if(!cap||process_getcwd(self,cwd,sizeof(cwd))!=0){result=-RIX_EINVAL;break;}size_t n=0;while(cwd[n])++n;if(n+1>cap||copy_to_user(frame->rdi,cwd,n+1)!=0)result=-RIX_EFAULT;else result=(int64_t)(n+1);break;}
 case RIX_SYS_GETUID:result=(int64_t)process_uid(self);break;
 case RIX_SYS_GETGID:result=(int64_t)process_gid(self);break;
 case RIX_SYS_SETUID:if(process_uid(self)==0&&!process_has_capability(self,RIX_CAP_SETUID))result=-RIX_EACCES;else result=process_setuid(self,(uint32_t)frame->rdi)==0?0:-RIX_EINVAL;break;
 case RIX_SYS_SETGID:if(process_uid(self)==0&&!process_has_capability(self,RIX_CAP_SETGID))result=-RIX_EACCES;else result=process_setgid(self,(uint32_t)frame->rdi)==0?0:-RIX_EINVAL;break;
 case RIX_SYS_GETGROUPS:{size_t count=(size_t)frame->rdi,actual=0;uint32_t groups[RIX_PROCESS_GROUP_MAX];if(count>RIX_PROCESS_GROUP_MAX||process_getgroups(self,groups,count,&actual)!=0){result=-RIX_EINVAL;break;}if(count&&actual&&copy_to_user(frame->rsi,groups,actual*sizeof(groups[0]))!=0){result=-RIX_EFAULT;break;}result=(int64_t)actual;break;}
 case RIX_SYS_SETGROUPS:{size_t count=(size_t)frame->rdi;uint32_t groups[RIX_PROCESS_GROUP_MAX];if(count>RIX_PROCESS_GROUP_MAX||(count&&copy_from_user(groups,frame->rsi,count*sizeof(groups[0]))!=0)||process_setgroups(self,groups,count)!=0)result=-RIX_EINVAL;else result=0;break;}
 case RIX_SYS_GETSID:{pid_t target=frame->rdi?((pid_t)frame->rdi):self;pid_t session;if(process_get_session(target,&session)!=0)result=-RIX_ESRCH;else if(copy_to_user(frame->rsi,&session,sizeof(session))!=0)result=-RIX_EFAULT;else result=0;break;}
 case RIX_SYS_GETCAP:{uint64_t caps;if(process_get_capabilities(self,&caps)!=0||copy_to_user(frame->rdi,&caps,sizeof(caps))!=0)result=-RIX_EFAULT;else result=0;break;}
 case RIX_SYS_DROPCAP:if(!capability_valid(frame->rdi)||process_drop_capabilities(self,frame->rdi)!=0)result=-RIX_EINVAL;else result=0;break;
 case RIX_SYS_GETAUDITUID:{uint32_t audit_uid;if(process_get_audit_uid(self,&audit_uid)!=0||copy_to_user(frame->rdi,&audit_uid,sizeof(audit_uid))!=0)result=-RIX_EFAULT;else result=0;break;}
 case RIX_SYS_SETAUDITUID:if(!process_has_capability(self,RIX_CAP_AUDIT_ADMIN)){result=-RIX_EACCES;break;}result=process_set_audit_uid(self,(uint32_t)frame->rdi)==0?0:-RIX_EINVAL;break;
 case RIX_SYS_DELEGATECAP:if(!process_has_capability(self,RIX_CAP_DELEGATE)){result=-RIX_EACCES;break;}if(!capability_valid(frame->rsi)||(frame->rsi&RIX_CAP_DELEGATE)){result=-RIX_EINVAL;break;}result=process_delegate_capabilities(self,(pid_t)frame->rdi,frame->rsi)==0?0:-RIX_EACCES;break;
 case RIX_SYS_SETSID:{rix_process_t*p=process_lookup(self);pid_t old_session=p?p->session:0,old_group=p?p->process_group:0,session;if(process_create_session(self,&session)!=0){result=-RIX_EINVAL;break;}if(copy_to_user(frame->rdi,&session,sizeof(session))!=0){(void)process_set_session(self,old_session);(void)process_set_group(self,old_group);result=-RIX_EFAULT;}else result=0;break;}
 case RIX_SYS_TTY_ATTACH:{rix_process_t*p=process_lookup(self);if(!p||!process_is_session_leader(self)||!process_has_capability(self,RIX_CAP_TTY_ADMIN)){result=-RIX_EACCES;break;}int rc=tty_attach_session((unsigned)frame->rdi,(uint32_t)p->session,(uint32_t)p->process_group);result=rc==0?0:(rc==-2?-RIX_EACCES:-RIX_EINVAL);break;}
 case RIX_SYS_TTY_DETACH:{rix_process_t*p=process_lookup(self);if(!p||!process_is_session_leader(self)||!process_has_capability(self,RIX_CAP_TTY_ADMIN)){result=-RIX_EACCES;break;}int rc=tty_detach_session((unsigned)frame->rdi,(uint32_t)p->session);result=rc==0?0:(rc==-2?-RIX_EACCES:-RIX_EINVAL);break;}
 case RIX_SYS_SESSION_LOGIN:{rix_process_t*p=process_lookup(self);pid_t old_session=p?p->session:0,old_group=p?p->process_group:0,session;if(!p||!process_has_capability(self,RIX_CAP_SESSION_ADMIN)){result=-RIX_EACCES;break;}if(process_create_session(self,&session)!=0){result=-RIX_EINVAL;break;}
int rc=tty_attach_session((unsigned)frame->rdi,(uint32_t)session,(uint32_t)p->process_group);if(rc!=0||copy_to_user(frame->rsi,&session,sizeof(session))!=0){if(rc==0)(void)tty_detach_session((unsigned)frame->rdi,(uint32_t)session);(void)process_set_session(self,old_session);(void)process_set_group(self,old_group);result=rc==-2?-RIX_EACCES:(rc!=0?-RIX_EINVAL:-RIX_EFAULT);}else result=0;break;}
 case RIX_SYS_SESSION_LOGOUT:{rix_process_t*p=process_lookup(self);if(!p||!process_is_session_leader(self)){result=-RIX_EACCES;break;}
pid_t session=p->session;for(unsigned tty=0;tty<RIX_TTY_COUNT;tty++)(void)tty_detach_session(tty,(uint32_t)session);result=process_logout_session(self,0)==0&&process_leave_session(self)==0?0:-RIX_EINVAL;break;}
 case RIX_SYS_LIST_SESSIONS:{size_t capacity=(size_t)frame->rsi,total=0;rix_session_info_t snapshot[RIX_SESSION_MAX];if(capacity>RIX_SESSION_MAX||process_list_sessions(snapshot,capacity,&total)!=0){result=-RIX_EINVAL;break;}if(copy_to_user(frame->rdx,&total,sizeof(total))!=0||(total&&copy_to_user(frame->rdi,snapshot,total*sizeof(snapshot[0]))!=0))result=-RIX_EFAULT;else result=(int64_t)total;break;}
 case RIX_SYS_CLOCK_GETTIME:{rix_timespec_t now;if(time_realtime(&now)!=0||copy_to_user(frame->rdi,&now,sizeof(now))!=0)result=-RIX_EINVAL;else result=0;break;}
 case RIX_SYS_NANOSLEEP:{rix_timespec_t request;if(copy_from_user(&request,frame->rdi,sizeof(request))!=0||request.nsec>=1000000000ULL){result=-RIX_EINVAL;break;}if(request.sec>UINT64_MAX/1000000000ULL||request.sec*1000000000ULL>UINT64_MAX-request.nsec){result=-RIX_EINVAL;break;}uint64_t duration=request.sec*1000000000ULL+request.nsec;uint64_t start=time_monotonic_ns();if(duration>UINT64_MAX-start){result=-RIX_EINVAL;break;}uint64_t deadline=start+duration;while(time_monotonic_ns()<deadline){if(syscall_interrupted(self)){result=-RIX_EINTR;break;}scheduler_yield();}if(result!=-(int64_t)RIX_EINTR)result=0;break;}
 case RIX_SYS_SHM_CREATE:{uint32_t id;if(shm_create(frame->rdi,(rix_shm_id_t*)&id)!=0){result=-RIX_EINVAL;break;}if(copy_to_user(frame->rsi,&id,sizeof(id))!=0){(void)shm_destroy(id);result=-RIX_EFAULT;break;}result=0;break;}
 case RIX_SYS_SHM_MAP:{uint32_t id=(uint32_t)frame->rdi;uint64_t va=frame->rsi,flags=frame->rdx;if(shm_map(id,self,va,flags)!=0)result=-RIX_EINVAL;else result=0;break;}
 case RIX_SYS_SHM_UNMAP:{uint32_t id=(uint32_t)frame->rdi;if(shm_unmap(id,self,frame->rsi)!=0)result=-RIX_EINVAL;else result=0;break;}
 case RIX_SYS_SHM_DESTROY:if(shm_destroy((uint32_t)frame->rdi)!=0)result=-RIX_EINVAL;else result=0;break;
 default:break;
 }
 frame->rax=(uint64_t)result;
}
void syscall_init(void){}
