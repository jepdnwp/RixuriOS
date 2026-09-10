#include "unistd.h"
#include <stddef.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <pthread.h>

static size_t length(const char *s){size_t n=0;while(s&&s[n])++n;return n;}
static void out(const char *s){(void)write(1,s,length(s));}
static void out_num(long v){char b[24];int n=0;int neg=0;if(v<0){neg=1;v=-v;}if(!v)b[n++]='0';while(v){b[n++]=(char)('0'+v%10);v/=10;}if(neg)b[n++]='-';while(n)(void)write(1,&b[--n],1);}
static int failures;
static void check(const char *group,int ok){out("posix:");out(group);out(ok?"=PASS\n":"=FAIL\n");if(!ok)++failures;}

static int exit_pipe_fd = -1;
static void exit_handler(void){if(exit_pipe_fd>=0)(void)write(exit_pipe_fd,"H",1);}

int program_main(int argc,char **argv,char **envp){
    (void)argc;(void)argv;(void)envp;
    struct timespec now={0,0};
    check("clock",clock_gettime(CLOCK_REALTIME,&now)==0&&now.tv_sec>0);
    check("clock-bad",clock_gettime(99,&now)!=0&&errno==RIX_EINVAL&&clock_gettime(CLOCK_REALTIME,0)!=0);
    struct timespec zero={0,0},bad={0,1000000000L};
    check("nanosleep",nanosleep(&zero,0)==0&&nanosleep(&bad,0)!=0&&errno==RIX_EINVAL);
    struct timeval moment={0,0};
    struct timespec before={0,0},after={0,0};
    int time_ok=clock_gettime(CLOCK_REALTIME,&before)==0&&gettimeofday(&moment,0)==0&&clock_gettime(CLOCK_REALTIME,&after)==0;
    time_ok=time_ok&&moment.tv_sec>=before.tv_sec&&moment.tv_sec<=after.tv_sec&&moment.tv_usec>=0&&moment.tv_usec<1000000;
    check("gettimeofday",time_ok);
    check("sysconf",getpagesize()==4096&&sysconf(_SC_PAGESIZE)==4096L&&sysconf(_SC_CLK_TCK)==100L&&sysconf(_SC_OPEN_MAX)==64L&&sysconf(9999)==-1L);
    errno=0;
    check("mmap",mmap(0,4096,PROT_READ,MAP_PRIVATE|MAP_ANONYMOUS,-1,0)==MAP_FAILED&&errno==RIX_ENOSYS);
    check("mmap-bad",mmap(0,0,PROT_READ,MAP_PRIVATE|MAP_ANONYMOUS,-1,0)==MAP_FAILED&&errno==RIX_EINVAL);
    {char stack_top;check("munmap",munmap(&stack_top,1)!=0&&errno==RIX_ENOSYS);}
    {char stack_top;errno=0;int rc=mprotect(&stack_top,4096,PROT_READ);int ec=errno;out("posix:mprotect-diag rc=");out_num(rc);out(" errno=");out_num(ec);out("\n");check("mprotect",rc!=0&&ec==RIX_ENOSYS);}
    {struct pollfd pfd={0,POLLIN,0};check("poll",poll(&pfd,1,0)!=0&&errno==RIX_ENOSYS);}
    check("ioctl",ioctl(1,0)!=0&&errno==RIX_ENOSYS);
    check("signal",signal(SIGTERM,SIG_IGN)==SIG_ERR&&errno==RIX_ENOSYS);
    check("sigaction",sigaction(SIGTERM,0,0)!=0&&errno==RIX_ENOSYS);
    {sigset_t pending=0;check("sigpending",sigpending(&pending)==0&&pending==0);}
    check("socket-domain",socket(99,SOCK_DGRAM,0)<0&&errno==RIX_EAFNOSUPPORT);
    check("socket-raw",socket(AF_INET,SOCK_RAW,0)<0&&errno==RIX_EPROTONOSUPPORT);
    check("fstat",0==0&&fstat(1,0)!=0&&errno==RIX_ENOSYS);
    {rix_stat_t st;check("lstat",lstat("/",(rix_stat_t*)&st)!=0&&errno==RIX_ENOSYS);}
    check("listen",listen(0,1)!=0&&errno==RIX_ENOSYS);
    check("setsockopt",setsockopt(0,SOL_SOCKET,99,0,0)!=0&&errno==RIX_ENOPROTOOPT);
    /* UDP loopback through the POSIX spelling. */
    int udp_ok=0;
    int fd=socket(AF_INET,SOCK_DGRAM,0);
    if(fd>=0){
        struct sockaddr_in addr;addr.sin_family=AF_INET;addr.sin_port=htons(41001);
        addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        for(size_t i=0;i<sizeof(addr.sin_zero);++i)addr.sin_zero[i]=0;
        if(bind(fd,(struct sockaddr*)&addr,sizeof(addr))==0&&
           setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,0,0)==0){
            static const char message[]="posix-udp";
            if(sendto(fd,message,sizeof(message),0,(struct sockaddr*)&addr,sizeof(addr))==(ssize_t)sizeof(message)){
                char buffer[64];struct sockaddr_in source;socklen_t source_len=sizeof(source);
                ssize_t got=recvfrom(fd,buffer,sizeof(buffer),0,(struct sockaddr*)&source,&source_len);
                if(got==(ssize_t)sizeof(message)&&memcmp(buffer,message,sizeof(message))==0&&
                   source.sin_family==AF_INET&&source.sin_port==htons(41001)&&
                   source.sin_addr.s_addr==htonl(INADDR_LOOPBACK))udp_ok=1;
            }
        }
        if(close(fd)!=0)udp_ok=0;
        /* Rebind the same port: proves close() released the socket. */
        int again=0;
        for(int i=0;i<20&&!again;++i){
            int second=socket(AF_INET,SOCK_DGRAM,0);
            if(second<0)break;
            if(bind(second,(struct sockaddr*)&addr,sizeof(addr))==0&&close(second)==0)again=1;
            else{(void)close(second);break;}
        }
        if(!again)udp_ok=0;
    }
    check("udp-loopback",udp_ok);
    /* Empty queue surfaces as EAGAIN, not success. */
    int empty_ok=0;
    int idle=socket(AF_INET,SOCK_DGRAM,0);
    if(idle>=0){
        char buffer[16];
        if(recv(idle,buffer,sizeof(buffer),0)<0&&errno==RIX_EAGAIN)empty_ok=1;
        (void)close(idle);
    }
    check("udp-empty",empty_ok);
    /* exit()/atexit() through fork + pipe + wait status macros. */
    int life_ok=0;
    int pair[2];
    if(pipe(pair)==0){
        rix_pid_t child=fork();
        if(child==(rix_pid_t)-1){(void)close(pair[0]);(void)close(pair[1]);}
        else if(child==0){
            (void)close(pair[0]);
            exit_pipe_fd=pair[1];
            if(atexit(exit_handler)!=0)_exit(9);
            exit(5);
            _exit(8);
        }else{
            char mark=0;(void)close(pair[1]);
            ssize_t got=read(pair[0],&mark,1);
            (void)close(pair[0]);
            uint64_t status=0;
            rix_pid_t done=wait(child,&status);
            if(got==1&&mark=='H'&&done==child&&WIFEXITED(status)&&WEXITSTATUS(status)==5)life_ok=1;
        }
    }
    check("exit-atexit",life_ok);
    /* Pure POSIX helpers on-target. */
    int parsed=0;char text[16];
    check("sscanf",sscanf("8080","%d",&parsed)==1&&parsed==8080);
    {
        char *go_argv[]={"posix-test","-n","42"};
        optind=1;opterr=0;
        int seen=0;int option;
        while((option=getopt(3,go_argv,"n:"))!=-1)if(option=='n'&&optarg&&strcmp(optarg,"42")==0)seen=1;
        check("getopt-run",seen&&optind==3);
    }
    {
        uint32_t address=0;
        int inet_ok=inet_pton(AF_INET,"127.0.0.1",&address)==1&&ntohl(address)==INADDR_LOOPBACK;
        struct in_addr loop;loop.s_addr=htonl(INADDR_LOOPBACK);
        inet_ok=inet_ok&&inet_ntop(AF_INET,&loop.s_addr,text,sizeof(text))!=0&&strcmp(text,"127.0.0.1")==0;
        check("inet",inet_ok);
    }
    {
        pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
        int mutex_ok=pthread_mutex_lock(&mutex)==0&&pthread_mutex_trylock(&mutex)==RIX_EBUSY;
        mutex_ok=mutex_ok&&pthread_mutex_unlock(&mutex)==0&&pthread_mutex_destroy(&mutex)==0;
        mutex_ok=mutex_ok&&pthread_create(0,0,0,0)==RIX_ENOSYS;
        check("pthread",mutex_ok);
    }
    out(failures?"posix=FAIL\n":"posix=PASS\n");
    return failures?1:0;
}

int main(int argc,char **argv,char **envp){return program_main(argc,argv,envp);}
