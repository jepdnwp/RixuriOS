#include "unistd.h"
#include "errno.h"
#include "signal.h"
#include "fcntl.h"
#include "string.h"
#include <stdarg.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

static long rix_sys(long n,long a,long b,long c){long r;__asm__ volatile("int $0x80":"=a"(r):"a"(n),"D"(a),"S"(b),"d"(c):"rcx","r11","memory");return r;}
static long rix_sys4(long n,long a,long b,long c,long d){long r;register long r10 __asm__("r10")=d;__asm__ volatile("int $0x80":"=a"(r):"a"(n),"D"(a),"S"(b),"d"(c),"r"(r10):"rcx","r11","memory");return r;}
static long rix_sys6(long n,long a,long b,long c,long d,long e,long f){long r;register long r10 __asm__("r10")=d;register long r8 __asm__("r8")=e;register long r9 __asm__("r9")=f;__asm__ volatile("int $0x80":"=a"(r):"a"(n),"D"(a),"S"(b),"d"(c),"r"(r10),"r"(r8),"r"(r9):"rcx","r11","memory");return r;}
static long rix_int_result(long result){if(result<0){errno=(int)-result;return -1;}return result;}
static rix_ssize_t rix_ssize_result(long result){if(result<0){errno=(int)-result;return (rix_ssize_t)-1;}return (rix_ssize_t)result;}
static rix_pid_t rix_pid_result(long result){if(result<0){errno=(int)-result;return (rix_pid_t)-1;}return (rix_pid_t)result;}

rix_ssize_t read(int fd,void*buf,size_t count){return rix_ssize_result(rix_sys(0,fd,(long)buf,(long)count));}
rix_ssize_t write(int fd,const void*buf,size_t count){return rix_ssize_result(rix_sys(1,fd,(long)buf,(long)count));}
int openat(int dirfd,const char*path,uint32_t flags,uint32_t mode){return(int)rix_int_result(rix_sys4(2,dirfd,(long)path,flags,mode));}
int open(const char *path,uint32_t flags,...){uint32_t mode=0;va_list arguments;va_start(arguments,flags);if(flags&4u)mode=va_arg(arguments,unsigned);va_end(arguments);return openat(-100,path,flags,mode);}
int creat(const char *path,uint32_t mode){return open(path,4u|8u|1u,mode);}
int mkdir(const char *path,uint32_t mode){return(int)rix_int_result(rix_sys(83,(long)path,mode,0));}
int rmdir(const char *path){return(int)rix_int_result(rix_sys(84,(long)path,0,0));}
int unlink(const char *path){return(int)rix_int_result(rix_sys(87,(long)path,0,0));}
int link(const char *old_path,const char *new_path){return(int)rix_int_result(rix_sys(86,(long)old_path,(long)new_path,0));}
int getdents(int fd,rix_dirent_t *entries,size_t capacity,size_t *count){return(int)rix_int_result(rix_sys4(78,fd,(long)entries,capacity,(long)count));}
int getdents64(int fd,rix_dirent_t *entries,size_t capacity,size_t *count){return getdents(fd,entries,capacity,count);}
off_t lseek(int fd,off_t offset,int whence){return(off_t)rix_int_result(rix_sys(8,fd,(long)offset,whence));}
int stat(const char *path,rix_stat_t *out){return(int)rix_int_result(rix_sys(4,(long)path,(long)out,0));}
int access(const char *path,int mode){if(!path||mode<0||(mode&~(R_OK|W_OK|X_OK))){errno=RIX_EINVAL;return -1;}rix_stat_t st;if(stat(path,&st)!=0)return -1;if(mode==F_OK)return 0;uint32_t permissions=st.mode&0777u;if((mode&R_OK)&&!(permissions&0444u)){errno=RIX_EACCES;return -1;}if((mode&W_OK)&&!(permissions&0222u)){errno=RIX_EACCES;return -1;}if((mode&X_OK)&&!(permissions&0111u)){errno=RIX_EACCES;return -1;}return 0;}
int fcntl(int fd,int command,...){long argument=0;va_list arguments;va_start(arguments,command);if(command==F_DUPFD||command==F_SETFD||command==F_SETFL)argument=va_arg(arguments,int);va_end(arguments);return(int)rix_int_result(rix_sys(143,fd,command,argument));}
int close(int fd){return(int)rix_int_result(rix_sys(3,fd,0,0));}
int pipe(int fds[2]){return(int)rix_int_result(rix_sys(22,(long)fds,0,0));}
int dup(int old_fd){return(int)rix_int_result(rix_sys(32,old_fd,0,0));}
int dup2(int old_fd,int new_fd){return(int)rix_int_result(rix_sys(33,old_fd,new_fd,0));}
int close_pipes_except(int keep_fd0,int keep_fd1){return(int)rix_int_result(rix_sys(248,keep_fd0,keep_fd1,0));}
rix_pid_t spawn(const char *name,const void *image,size_t image_size){return rix_pid_result(rix_sys(134,(long)name,(long)image,(long)image_size));}
rix_pid_t fork(void){return rix_pid_result(rix_sys(57,0,0,0));}
rix_pid_t wait(rix_pid_t child,uint64_t*status){return rix_pid_result(rix_sys(61,(long)child,(long)status,0));}
rix_pid_t waitpid(rix_pid_t child,uint64_t*status,uint32_t options){return rix_pid_result(rix_sys(247,(long)child,(long)status,(long)options));}
/* POSIX clock layer. struct timespec is layout-identical to the kernel
 * timespec (two consecutive 64-bit words), so the pointer crosses the
 * ABI unchanged. */
_Static_assert(sizeof(struct timespec) == 16, "timespec ABI width");
_Static_assert(sizeof(rix_timespec_t) == sizeof(struct timespec), "rix/POSIX timespec match");
int nanosleep(const struct timespec *request,struct timespec *remaining){if(!request||request->tv_sec<0||request->tv_nsec<0||request->tv_nsec>=1000000000L){errno=RIX_EINVAL;return -1;}return(int)rix_int_result(rix_sys(35,(long)request,(long)remaining,0));}
unsigned sleep(unsigned seconds){struct timespec request={seconds,0},remaining={0,0};if(nanosleep(&request,&remaining)==0)return 0;return(unsigned)remaining.tv_sec+(remaining.tv_nsec!=0);}
int usleep(unsigned usec){if(usec>=1000000u){errno=RIX_EINVAL;return -1;}struct timespec request={0,(long)usec*1000L};return nanosleep(&request,0);}
/* The kernel exposes one realtime source; MONOTONIC is accepted and
 * documented as same-source until Phase 14 splits the clocks. */
int clock_gettime(clockid_t clock,struct timespec *out){if(!out||(clock!=CLOCK_REALTIME&&clock!=CLOCK_MONOTONIC)){errno=RIX_EINVAL;return -1;}return(int)rix_int_result(rix_sys(13,(long)out,0,0));}
int chdir(const char *path){return(int)rix_int_result(rix_sys(80,(long)path,0,0));}
int getcwd(char *buffer,size_t capacity){return(int)rix_int_result(rix_sys(79,(long)buffer,(long)capacity,0));}
uint32_t getuid(void){return(uint32_t)rix_sys(102,0,0,0);}
uint32_t getgid(void){return(uint32_t)rix_sys(104,0,0,0);}
int setuid(uint32_t uid){return(int)rix_int_result(rix_sys(105,(long)uid,0,0));}
int setgid(uint32_t gid){return(int)rix_int_result(rix_sys(106,(long)gid,0,0));}
int chmod(const char *path,uint32_t mode){return(int)rix_int_result(rix_sys(90,(long)path,(long)mode,0));}
int chown(const char *path,uint32_t uid,uint32_t gid){return(int)rix_int_result(rix_sys(91,(long)path,(long)uid,(long)gid));}
int rename(const char *old_path,const char *new_path){return(int)rix_int_result(rix_sys(82,(long)old_path,(long)new_path,0));}
int getacl(const char *path,rix_acl_t *out){return(int)rix_int_result(rix_sys(117,(long)path,(long)out,0));}
int setacl(const char *path,const rix_acl_t *acl){return(int)rix_int_result(rix_sys(118,(long)path,(long)acl,0));}
int clearacl(const char *path){return(int)rix_int_result(rix_sys(119,(long)path,0,0));}
int get_session(rix_pid_t pid,rix_pid_t *out_session){return(int)rix_int_result(rix_sys(120,(long)pid,(long)out_session,0));}
int create_session(rix_pid_t *out_session){return(int)rix_int_result(rix_sys(121,(long)out_session,0,0));}
int attach_tty(uint32_t tty_id){return(int)rix_int_result(rix_sys(122,(long)tty_id,0,0));}
int detach_tty(uint32_t tty_id){return(int)rix_int_result(rix_sys(123,(long)tty_id,0,0));}
int login_session(uint32_t tty_id,rix_pid_t *out_session){return(int)rix_int_result(rix_sys(124,(long)tty_id,(long)out_session,0));}
int logout_session(void){return(int)rix_int_result(rix_sys(125,0,0,0));}
int list_sessions(rix_session_info_t*sessions,size_t capacity,size_t*count){return(int)rix_int_result(rix_sys(126,(long)sessions,(long)capacity,(long)count));}
int list_processes(rix_process_info_t*procs,size_t capacity,size_t*count){return(int)rix_int_result(rix_sys(138,(long)procs,(long)capacity,(long)count));}
int get_capabilities(uint64_t*out){return(int)rix_int_result(rix_sys(132,(long)out,0,0));}
int drop_capabilities(uint64_t mask){return(int)rix_int_result(rix_sys(133,(long)mask,0,0));}
int get_audit_uid(uint32_t*out){return(int)rix_int_result(rix_sys(135,(long)out,0,0));}
int set_audit_uid(uint32_t uid){return(int)rix_int_result(rix_sys(136,(long)uid,0,0));}
int delegate_capabilities(rix_pid_t child,uint64_t mask){return(int)rix_int_result(rix_sys(137,(long)child,(long)mask,0));}
int getgroups(size_t capacity,uint32_t *groups){return(int)rix_int_result(rix_sys(115,(long)capacity,(long)groups,0));}
int setgroups(size_t count,const uint32_t *groups){return(int)rix_int_result(rix_sys(116,(long)count,(long)groups,0));}
int execve(const char *path,char *const argv[],char *const envp[]){return(int)rix_int_result(rix_sys(59,(long)path,(long)argv,(long)envp));}
rix_pid_t getpid(void){return(rix_pid_t)rix_sys(39,0,0,0);}
rix_pid_t getppid(void){return rix_pid_result(rix_sys(140,0,0,0));}
int isatty(int fd){return(int)rix_int_result(rix_sys(141,fd,0,0));}
int kill(rix_pid_t pid,uint32_t signal){return(int)rix_int_result(rix_sys(62,(long)pid,(long)signal,0));}
static int signal_bit_valid(int signal){return signal>=1&&signal<=64;}
int sigemptyset(sigset_t *set){if(!set){errno=RIX_EFAULT;return -1;}*set=0;return 0;}
int sigfillset(sigset_t *set){if(!set){errno=RIX_EFAULT;return -1;}*set=UINT64_MAX;return 0;}
int sigaddset(sigset_t *set,int signal){if(!set||!signal_bit_valid(signal)){errno=RIX_EINVAL;return -1;}*set|=1ULL<<(signal-1);return 0;}
int sigdelset(sigset_t *set,int signal){if(!set||!signal_bit_valid(signal)){errno=RIX_EINVAL;return -1;}*set&=~(1ULL<<(signal-1));return 0;}
int sigismember(const sigset_t *set,int signal){if(!set||!signal_bit_valid(signal)){errno=RIX_EINVAL;return -1;}return(*set&(1ULL<<(signal-1)))!=0;}
int sigpending(sigset_t *set){if(!set){errno=RIX_EFAULT;return -1;}return(int)rix_int_result(rix_sys(127,(long)set,0,0));}
int sigprocmask(int how,const sigset_t *set,sigset_t *oldset){return(int)rix_int_result(rix_sys4(142,how,(long)set,0,(long)oldset));}
int raise(int signal){return kill(getpid(),(uint32_t)signal);}
sighandler_t signal(int number,sighandler_t handler){(void)number;(void)handler;errno=RIX_ENOSYS;return SIG_ERR;}
int sigaction(int number,const struct sigaction *action,struct sigaction *old){(void)number;(void)action;(void)old;errno=RIX_ENOSYS;return -1;}
int pause(void){for(;;){sigset_t pending=0;if(sigpending(&pending)!=0)return -1;if(pending){errno=RIX_EINTR;return -1;}struct timespec step={0,1000000L};if(nanosleep(&step,0)!=0)return -1;}}
_Noreturn void _Exit(int status){_exit(status);}
int fstat(int fd,rix_stat_t *out){(void)fd;(void)out;errno=RIX_ENOSYS;return -1;}
int lstat(const char *path,rix_stat_t *out){(void)path;(void)out;errno=RIX_ENOSYS;return -1;}
/* RIX_SYS_MMAP/MPROTECT/MUNMAP have no kernel handler: length is still
 * validated so callers get EINVAL for empty ranges and ENOSYS otherwise. */
void *mmap(void *address,size_t length,int protection,int flags,int fd,off_t offset){(void)protection;(void)flags;(void)fd;(void)offset;if(!length){errno=RIX_EINVAL;return MAP_FAILED;}long rc=rix_sys6(9,(long)address,(long)length,(long)protection,(long)flags,(long)fd,(long)offset);if(rc<0){errno=(int)-rc;return MAP_FAILED;}return(void*)rc;}
int munmap(void *address,size_t length){if(!length){errno=RIX_EINVAL;return -1;}return(int)rix_int_result(rix_sys(11,(long)address,(long)length,0));}
int mprotect(void *address,size_t length,int protection){if(!length){errno=RIX_EINVAL;return -1;}return(int)rix_int_result(rix_sys(10,(long)address,(long)length,(long)protection));}
int poll(struct pollfd *fds,nfds_t count,int timeout_ms){if(count&&!fds){errno=RIX_EFAULT;return -1;}return(int)rix_int_result(rix_sys(7,(long)fds,(long)count,(long)timeout_ms));}
int ioctl(int fd,unsigned long command,...){(void)command;return(int)rix_int_result(rix_sys(16,(long)fd,0,0));}
/* POSIX socket spelling over the native socket syscalls. Kernel raw
 * negatives are remapped: -2 (ARP pending/device busy) and -3 (empty
 * queue) become EAGAIN, -4 (payload larger than the receiver capacity)
 * becomes EMSGSIZE, -110 (connect timeout) becomes ETIMEDOUT. */
static rix_ssize_t rix_socket_result(long result){if(result>=0)return(rix_ssize_t)result;long code=-result;if(code==2||code==3)code=RIX_EAGAIN;else if(code==4)code=RIX_EMSGSIZE;else if(code==110)code=RIX_ETIMEDOUT;errno=(int)code;return(rix_ssize_t)-1;}
static int sockaddr_to_endpoint(const struct sockaddr *address,socklen_t length,rix_net_endpoint_t *out){if(!address||!out||length<sizeof(struct sockaddr_in)){errno=RIX_EINVAL;return -1;}if(address->sa_family!=AF_INET){errno=RIX_EAFNOSUPPORT;return -1;}const struct sockaddr_in *in=(const struct sockaddr_in*)address;out->address=ntohl(in->sin_addr.s_addr);out->port=ntohs(in->sin_port);return 0;}
static int endpoint_to_sockaddr(rix_net_endpoint_t endpoint,struct sockaddr *address,socklen_t *length){if(!address||!length||*length<sizeof(struct sockaddr_in)){if(length)*length=sizeof(struct sockaddr_in);errno=RIX_EINVAL;return -1;}struct sockaddr_in *in=(struct sockaddr_in*)address;in->sin_family=AF_INET;in->sin_port=htons(endpoint.port);in->sin_addr.s_addr=htonl(endpoint.address);for(size_t i=0;i<sizeof(in->sin_zero);++i)in->sin_zero[i]=0;*length=sizeof(struct sockaddr_in);return 0;}
int socket(int domain,int type,int protocol){(void)protocol;if(domain!=AF_INET){errno=RIX_EAFNOSUPPORT;return -1;}int native=0;if(type==SOCK_DGRAM)native=RIX_NET_SOCKET_UDP;else if(type==SOCK_STREAM)native=RIX_NET_SOCKET_TCP;else{errno=(type==SOCK_RAW)?RIX_EPROTONOSUPPORT:RIX_EINVAL;return -1;}long rc=rix_sys(41,native,0,0);if(rc<0){errno=(int)-rc;return -1;}return(int)rc;}
int bind(int fd,const struct sockaddr *address,socklen_t length){rix_net_endpoint_t endpoint;if(sockaddr_to_endpoint(address,length,&endpoint)!=0)return -1;long rc=rix_sys(42,fd,(long)&endpoint,0);if(rc<0){errno=(int)-rc;return -1;}return 0;}
int connect(int fd,const struct sockaddr *address,socklen_t length){rix_net_endpoint_t endpoint;if(sockaddr_to_endpoint(address,length,&endpoint)!=0)return -1;if(!endpoint.port){errno=RIX_EINVAL;return -1;}long rc=rix_sys(43,fd,(long)&endpoint,0);return(int)rix_socket_result(rc);}
int listen(int fd,int backlog){(void)fd;(void)backlog;errno=RIX_ENOSYS;return -1;}
int accept(int fd,struct sockaddr *address,socklen_t *length){(void)fd;(void)address;(void)length;errno=RIX_ENOSYS;return -1;}
ssize_t sendto(int fd,const void *buffer,size_t length,int flags,const struct sockaddr *destination,socklen_t dest_len){if(flags){errno=RIX_EINVAL;return -1;}if(!buffer&&length){errno=RIX_EFAULT;return -1;}if(length>1500u){errno=RIX_EMSGSIZE;return -1;}rix_net_endpoint_t endpoint={0,0};const rix_net_endpoint_t *target=&endpoint;if(destination){if(sockaddr_to_endpoint(destination,dest_len,&endpoint)!=0)return -1;}long rc=rix_sys4(44,fd,(long)buffer,(long)length,(long)target);return rix_socket_result(rc);}
ssize_t recvfrom(int fd,void *buffer,size_t length,int flags,struct sockaddr *source,socklen_t *source_len){if(flags){errno=RIX_EINVAL;return -1;}if(!buffer&&length){errno=RIX_EFAULT;return -1;}rix_net_endpoint_t hop={0,0};long rc=rix_sys4(45,fd,(long)buffer,(long)length,(long)(source?&hop:0));rix_ssize_t result=rix_socket_result(rc);if(result>=0&&source&&endpoint_to_sockaddr(hop,source,source_len)!=0)return -1;return result;}
ssize_t send(int fd,const void *buffer,size_t length,int flags){return sendto(fd,buffer,length,flags,0,0);}
ssize_t recv(int fd,void *buffer,size_t length,int flags){return recvfrom(fd,buffer,length,flags,0,0);}
int shutdown(int fd,int how){(void)fd;(void)how;errno=RIX_ENOSYS;return -1;}
int setsockopt(int fd,int level,int option,const void *value,socklen_t length){(void)fd;(void)value;(void)length;if(level==SOL_SOCKET&&option==SO_REUSEADDR)return 0;errno=RIX_ENOPROTOOPT;return -1;}
int getsockopt(int fd,int level,int option,void *value,socklen_t *length){(void)fd;if(level==SOL_SOCKET&&option==SO_REUSEADDR&&value&&length&&*length>=sizeof(int)){*(int*)value=1;*length=sizeof(int);return 0;}errno=RIX_ENOPROTOOPT;return -1;}
int socket_open(int type){return(int)rix_int_result(rix_sys(41,type,0,0));}
int socket_bind(int fd,rix_net_endpoint_t endpoint){return(int)rix_int_result(rix_sys(42,fd,(long)&endpoint,0));}
int socket_connect(int fd,rix_net_endpoint_t endpoint){return(int)rix_int_result(rix_sys(43,fd,(long)&endpoint,0));}
int socket_send(int fd,const void*data,size_t length,rix_net_endpoint_t destination){return(int)rix_int_result(rix_sys4(44,fd,(long)data,(long)length,(long)&destination));}
int socket_receive(int fd,void*data,size_t capacity,rix_net_endpoint_t*source){return(int)rix_int_result(rix_sys4(45,fd,(long)data,(long)capacity,(long)source));}
rix_ssize_t getrandom(void*buffer,size_t length,uint32_t flags){return rix_ssize_result(rix_sys(139,(long)buffer,(long)length,(long)flags));}
int brk(void *address){long result=rix_sys(12,(long)address,0,0);if(result<0){errno=(int)-result;return -1;}return 0;}
void *sbrk(ptrdiff_t increment){long current=rix_sys(12,0,0,0);if(current<0){errno=(int)-current;return(void*)-1;}if((increment>0&&current>(long)UINTPTR_MAX-increment)||(increment<0&&current<(long)INTPTR_MIN-increment)){errno=RIX_EINVAL;return(void*)-1;}long requested=current+increment;long result=rix_sys(12,requested,0,0);if(result<0){errno=(int)-result;return(void*)-1;}return(void*)current;}
_Noreturn void _exit(int status){(void)rix_sys(60,status,0,0);for(;;)__asm__ volatile("hlt");}
