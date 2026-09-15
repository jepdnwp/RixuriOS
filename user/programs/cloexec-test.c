#include "unistd.h"
#include "fcntl.h"
#include "errno.h"
#include <stddef.h>
#include <stdint.h>
#include <sys/wait.h>

static size_t slen(const char *s){size_t n=0;while(s&&s[n])++n;return n;}
static void mark(const char *s){(void)write(1,s,slen(s));}
static int streq(const char *a,const char *b){
    size_t i=0;for(;;i++){if(a[i]!=b[i])return 0;if(!a[i])return 1;}
}
static int parse_int(const char *s,int *out){
    if(!s||!out)return -1;
    int v=0;size_t i=0;if(!s[0])return -1;
    while(s[i]){if(s[i]<'0'||s[i]>'9')return -1;v=v*10+(s[i]-'0');i++;}
    *out=v;return 0;
}
static void fmt_int(char *buf,size_t cap,int v){
    char tmp[16];int n=0;
    if(v==0){if(cap>1){buf[0]='0';buf[1]=0;}return;}
    while(v>0&&n<15){tmp[n++]=(char)('0'+v%10);v/=10;}
    size_t i=0;while(n>0&&i+1<cap){buf[i++]=tmp[--n];}
    if(i<cap)buf[i]=0;
}

int program_main(int argc,char **argv,char **envp){
    /* Child mode: cloexec-test child <fdA> <fdB> */
    if(argc>=4&&streq(argv[1],"child")){
        int fda=-1,fdb=-1;
        if(parse_int(argv[2],&fda)!=0||parse_int(argv[3],&fdb)!=0){mark("cloexec:child-badarg\n");_exit(2);}
        int ra=fcntl(fda,F_GETFD);
        (void)errno;
        int rb=fcntl(fdb,F_GETFD);
        if(ra>=0){mark("cloexec:child-cloexec-survived\n");_exit(1);}
        if(rb!=0){mark("cloexec:child-plain-lost\n");_exit(1);}
        mark("cloexec:child-exec-close PASS\n");
        (void)close(fdb);
        _exit(0);
    }
    int ok=1;
    /* Open with O_CLOEXEC: F_GETFD must be FD_CLOEXEC. */
    int fd=open("/bin/echo",O_RDONLY|O_CLOEXEC,0);
    if(fd<0){mark("cloexec:open-cloexec FAIL\n");return 1;}
    mark("cloexec:open-cloexec PASS\n");
    int fl=fcntl(fd,F_GETFD);
    if(fl!=FD_CLOEXEC){mark("cloexec:getfd FAIL\n");ok=0;}
    else mark("cloexec:getfd PASS\n");
    if(fcntl(fd,F_SETFD,0)!=0){mark("cloexec:setfd-clear FAIL\n");ok=0;}
    else if(fcntl(fd,F_GETFD)!=0){mark("cloexec:setfd-clear-verify FAIL\n");ok=0;}
    else mark("cloexec:setfd-clear PASS\n");
    if(fcntl(fd,F_SETFD,FD_CLOEXEC)!=0){mark("cloexec:setfd-set FAIL\n");ok=0;}
    else if(fcntl(fd,F_GETFD)!=FD_CLOEXEC){mark("cloexec:setfd-set-verify FAIL\n");ok=0;}
    else mark("cloexec:setfd-set PASS\n");
    if(fcntl(fd,F_SETFD,99)!=0||1){if(fcntl(fd,F_SETFD,99)==0){mark("cloexec:setfd-bad-accept FAIL\n");ok=0;}else mark("cloexec:setfd-bad-reject PASS\n");}
    /* dup clears CLOEXEC. */
    int dd=dup(fd);
    if(dd<0){mark("cloexec:dup FAIL\n");ok=0;}
    else{
        int df=fcntl(dd,F_GETFD);
        if(df!=0){mark("cloexec:dup-clear FAIL\n");ok=0;}
        else mark("cloexec:dup-clear PASS\n");
        (void)close(dd);
    }
    /* fcntl F_DUPFD clears as well. */
    int dm=fcntl(fd,F_DUPFD,0);
    if(dm<0){mark("cloexec:dupfd FAIL\n");ok=0;}
    else{
        if(fcntl(dm,F_GETFD)!=0){mark("cloexec:dupfd-clear FAIL\n");ok=0;}
        else mark("cloexec:dupfd-clear PASS\n");
        (void)close(dm);
    }
    /* dup2 clears as well. */
    int dt=dup(fd);
    if(dt>=0){
        (void)close(dt);
        int t2=open("/bin/echo",O_RDONLY,0);
        if(t2>=0){
            if(dup2(fd,t2)!=t2){mark("cloexec:dup2 FAIL\n");ok=0;}
            else if(fcntl(t2,F_GETFD)!=0){mark("cloexec:dup2-clear FAIL\n");ok=0;}
            else mark("cloexec:dup2-clear PASS\n");
            (void)close(t2);
        }
    }
    /* Exec test: fdA cloexec, fdB plain. */
    int fdA=open("/bin/echo",O_RDONLY|O_CLOEXEC,0);
    int fdB=open("/bin/echo",O_RDONLY,0);
    if(fdA<0||fdB<0){mark("cloexec:exec-open FAIL\n");ok=0;}
    else{
        char sa[16],sb[16];
        fmt_int(sa,sizeof(sa),fdA);fmt_int(sb,sizeof(sb),fdB);
        char *cargv[5];
        cargv[0]="cloexec-test";cargv[1]="child";cargv[2]=sa;cargv[3]=sb;cargv[4]=0;
        rix_pid_t child=fork();
        if(child==(rix_pid_t)-1){mark("cloexec:fork FAIL\n");ok=0;}
        else if(child==0){
            (void)execve("/usr/bin/cloexec-test",cargv,envp);
            mark("cloexec:exec FAIL\n");
            _exit(127);
        }else{
            uint64_t status=0;
            rix_pid_t w=waitpid(child,&status,0);
            if(w!=child||status!=0){mark("cloexec:exec-child FAIL\n");ok=0;}
            else mark("cloexec:exec-child PASS\n");
        }
        (void)close(fdA);(void)close(fdB);
    }
    (void)close(fd);
    if(ok){mark("cloexec:PASS\n");return 0;}
    mark("cloexec:FAIL\n");
    return 1;
}
