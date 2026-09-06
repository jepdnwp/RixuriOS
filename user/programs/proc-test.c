#include "unistd.h"
#include <stddef.h>
#include <stdint.h>

static size_t length(const char *s){size_t n=0;while(s&&s[n])++n;return n;}
static void out(const char *s){(void)write(1,s,length(s));}
static void mark(const char *s){(void)write(2,s,length(s));}

int program_main(int argc,char **argv,char **envp){
    (void)argc; (void)argv; (void)envp;
    volatile int passed=1;
    mark("proc:pipe\n");
    int fds[2]={-1,-1};
    if (pipe(fds)!=0) passed=0;
    mark(passed?"proc:pipe-after\n":"proc:pipe-fail\n");
    rix_pid_t writer=passed?fork():(rix_pid_t)-1;
    mark("proc:fork-after\n");
    if (writer==(rix_pid_t)-1) passed=0;
    if (writer==0) {
        (void)close(fds[0]);
        const char message[]="wake\n";
        if (write(fds[1],message,sizeof(message)-1u)!=(rix_ssize_t)(sizeof(message)-1u)) _exit(1);
        (void)close(fds[1]);
        _exit(0);
    }
    if (writer!=(rix_pid_t)-1) {
        (void)close(fds[1]);
        mark("proc:read\n");
        char buffer[8]={0};
        rix_ssize_t got=read(fds[0],buffer,sizeof(buffer)-1u);
        if (got!=5 || buffer[0]!='w' || buffer[4]!='\n') passed=0;
        (void)close(fds[0]);
        uint64_t writer_status=0;
        mark("proc:wait-writer\n");
        if (waitpid(writer,&writer_status,0)!=writer || writer_status!=0) passed=0;
    }
    mark("proc:fork-wnohang\n");
    rix_pid_t child=fork();
    if (child==(rix_pid_t)-1) passed=0;
    if (child==0) _exit(7);
    if (child!=(rix_pid_t)-1) {
        uint64_t child_status=0;
        mark("proc:wait-nohang\n");
        rix_pid_t probe=waitpid(child,&child_status,1u);
        if (probe!=0 && probe!=child) passed=0;
        if (probe==0 && waitpid(child,&child_status,0)!=child) passed=0;
        if (probe==child && child_status!=7) passed=0;
    }
    mark("proc:done\n");
    out(passed?"proc_pipe_wait=PASS\n":"proc_pipe_wait=FAIL\n");
    return passed?0:1;
}
