#include "unistd.h"
static long rix_sys(long n,long a,long b,long c){long r;__asm__ volatile("int $0x80":"=a"(r):"a"(n),"D"(a),"S"(b),"d"(c):"rcx","r11","memory");return r;}
rix_ssize_t write(int fd,const void*buf,size_t count){return(rix_ssize_t)rix_sys(1,fd,(long)buf,(long)count);}
int pipe(int fds[2]){return(int)rix_sys(22,(long)fds,0,0);}
int dup(int old_fd){return(int)rix_sys(32,old_fd,0,0);}
rix_pid_t spawn(const char *name,const void *image,size_t image_size){return(rix_pid_t)rix_sys(134,(long)name,(long)image,(long)image_size);}
rix_pid_t fork(void){return(rix_pid_t)rix_sys(57,0,0,0);}
int execve(const char *path,char *const argv[],char *const envp[]){return(int)rix_sys(59,(long)path,(long)argv,(long)envp);}
rix_pid_t getpid(void){return(rix_pid_t)rix_sys(39,0,0,0);}
_Noreturn void _exit(int status){(void)rix_sys(60,status,0,0);for(;;)__asm__ volatile("hlt");}
