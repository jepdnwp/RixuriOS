#include "unistd.h"
static long rix_sys(long n,long a,long b,long c){long r;__asm__ volatile("int $0x80":"=a"(r):"a"(n),"D"(a),"S"(b),"d"(c):"rcx","r11","memory");return r;}
static long rix_sys4(long n,long a,long b,long c,long d){long r;register long r10 __asm__("r10")=d;__asm__ volatile("int $0x80":"=a"(r):"a"(n),"D"(a),"S"(b),"d"(c),"r"(r10):"rcx","r11","memory");return r;}
rix_ssize_t read(int fd,void*buf,size_t count){return(rix_ssize_t)rix_sys(0,fd,(long)buf,(long)count);}
rix_ssize_t write(int fd,const void*buf,size_t count){return(rix_ssize_t)rix_sys(1,fd,(long)buf,(long)count);}
int openat(int dirfd,const char*path,uint32_t flags,uint32_t mode){return(int)rix_sys4(2,dirfd,(long)path,flags,mode);}
int mkdir(const char *path,uint32_t mode){return(int)rix_sys(83,(long)path,mode,0);}
int rmdir(const char *path){return(int)rix_sys(84,(long)path,0,0);}
int unlink(const char *path){return(int)rix_sys(87,(long)path,0,0);}
int link(const char *old_path,const char *new_path){return(int)rix_sys(86,(long)old_path,(long)new_path,0);}
int getdents(int fd,rix_dirent_t *entries,size_t capacity,size_t *count){return(int)rix_sys4(78,fd,(long)entries,capacity,(long)count);}
int stat(const char *path,rix_stat_t *out){return(int)rix_sys(4,(long)path,(long)out,0);}
int close(int fd){return(int)rix_sys(3,fd,0,0);}
int pipe(int fds[2]){return(int)rix_sys(22,(long)fds,0,0);}
int dup(int old_fd){return(int)rix_sys(32,old_fd,0,0);}
int dup2(int old_fd,int new_fd){return(int)rix_sys(33,old_fd,new_fd,0);}
int close_pipes_except(int keep_fd0,int keep_fd1){return(int)rix_sys(248,keep_fd0,keep_fd1,0);}
rix_pid_t spawn(const char *name,const void *image,size_t image_size){return(rix_pid_t)rix_sys(134,(long)name,(long)image,(long)image_size);}
rix_pid_t fork(void){return(rix_pid_t)rix_sys(57,0,0,0);}
rix_pid_t wait(rix_pid_t child,uint64_t*status){return(rix_pid_t)rix_sys(61,(long)child,(long)status,0);}
rix_pid_t waitpid(rix_pid_t child,uint64_t*status,uint32_t options){return(rix_pid_t)rix_sys(247,(long)child,(long)status,(long)options);}
int nanosleep(const rix_timespec_t *request, rix_timespec_t *remaining){return(int)rix_sys(35,(long)request,(long)remaining,0);}
int clock_gettime(rix_timespec_t *out){return(int)rix_sys(13,(long)out,0,0);}
int chdir(const char *path){return(int)rix_sys(80,(long)path,0,0);}
int getcwd(char *buffer,size_t capacity){return(int)rix_sys(79,(long)buffer,(long)capacity,0);}
uint32_t getuid(void){return(uint32_t)rix_sys(102,0,0,0);}
uint32_t getgid(void){return(uint32_t)rix_sys(104,0,0,0);}
int setuid(uint32_t uid){return(int)rix_sys(105,(long)uid,0,0);}
int setgid(uint32_t gid){return(int)rix_sys(106,(long)gid,0,0);}
int getgroups(size_t capacity,uint32_t *groups){return(int)rix_sys(115,(long)capacity,(long)groups,0);}
int setgroups(size_t count,const uint32_t *groups){return(int)rix_sys(116,(long)count,(long)groups,0);}
int execve(const char *path,char *const argv[],char *const envp[]){return(int)rix_sys(59,(long)path,(long)argv,(long)envp);}
rix_pid_t getpid(void){return(rix_pid_t)rix_sys(39,0,0,0);}
int kill(rix_pid_t pid,uint32_t signal){return(int)rix_sys(62,(long)pid,(long)signal,0);}
_Noreturn void _exit(int status){(void)rix_sys(60,status,0,0);for(;;)__asm__ volatile("hlt");}
