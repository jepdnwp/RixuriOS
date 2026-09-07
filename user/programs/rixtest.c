#include "unistd.h"
#include <stddef.h>
#include <stdint.h>

#define RIX_VFS_AT_FDCWD (-100)
#define RIX_VFS_O_WRONLY 1u
#define RIX_VFS_O_CREAT 4u
#define RIX_VFS_O_TRUNC 8u
#define RIX_WNOHANG 1u
#define RIX_SIGKILL 9u

typedef struct { const char *path; } test_case_t;

static size_t length(const char *s) { size_t n=0; while(s&&s[n])++n; return n; }
static void out(const char *s) { (void)write(1,s,length(s)); }
static void log_line(int fd,const char *s) { if(fd>=0)(void)write(fd,s,length(s)); }
static void out_num(uint64_t v) { char b[24];size_t n=0;if(!v){out("0");return;}while(v&&n<sizeof(b)){b[n++]=(char)('0'+v%10u);v/=10u;}while(n){char c=b[--n];(void)write(1,&c,1);} }

static int wait_with_timeout(rix_pid_t child,uint64_t *status) {
    rix_timespec_t pause={0,100000000};
    for(unsigned i=0;i<100u;++i){
        rix_pid_t r=waitpid(child,status,RIX_WNOHANG);
        if(r==child)return 0;
        if(r!=(rix_pid_t)0&&r!=(rix_pid_t)-1)return -1;
        (void)nanosleep(&pause,NULL);
    }
    (void)kill(child,RIX_SIGKILL);(void)waitpid(child,status,0);return 1;
}

static int run_one(const test_case_t *test,int logfd) {
    char *argv[]={(char *)test->path,NULL};
    char *envp[]={(char *)"PATH=/bin:/usr/bin:/sbin:/usr/sbin",(char *)"PWD=/",NULL};
    rix_pid_t child=fork();if(child==(rix_pid_t)-1)return -1;
    if(child==0){if(execve(test->path,argv,envp)!=0)_exit(127);_exit(127);}
    uint64_t status=0;int result=wait_with_timeout(child,&status);
    if(result==1){out("\033[1;31mTIMEOUT\033[0m ");out(test->path);out("\n");log_line(logfd,"TIMEOUT ");log_line(logfd,test->path);log_line(logfd,"\n");return 1;}
    if(result!=0||status!=0){out("\033[1;31mFAIL\033[0m ");out(test->path);out(" status=");out_num(status);out("\n");log_line(logfd,"FAIL ");log_line(logfd,test->path);log_line(logfd,"\n");return 1;}
    out("\033[1;32mPASS\033[0m ");out(test->path);out("\n");log_line(logfd,"PASS ");log_line(logfd,test->path);log_line(logfd,"\n");return 0;
}

static void skip_test(int logfd,const char *name,const char *why){
    out("SKIP ");out(name);out(" (");out(why);out(")\n");log_line(logfd,"SKIP ");log_line(logfd,name);log_line(logfd," ");log_line(logfd,why);log_line(logfd,"\n");
}

int program_main(int argc,char **argv,char **envp) {
    (void)envp;
    static const test_case_t smoke[]={
        {"/usr/bin/abi-negative"},{"/usr/bin/capdelegatecheck"},
        {"/usr/bin/capdelegatetest"},{"/usr/bin/sessiontest"},
        {"/usr/bin/sessionlisttest"},{"/usr/bin/killtest"}
    };
    static const test_case_t full[]={
        {"/usr/bin/credtest"},{"/usr/bin/auditcheck"},{"/usr/bin/metatest"},
        {"/usr/bin/renametest"},{"/usr/bin/proc-test"}
    };
    int logfd=openat(RIX_VFS_AT_FDCWD,"/usr/rixtest.log",RIX_VFS_O_WRONLY|RIX_VFS_O_CREAT|RIX_VFS_O_TRUNC,0644u);
    out("\033[1;36mRixuriOS native test runner\033[0m\n");out("log: /usr/rixtest.log\n");
    out("mode: safe native smoke (use --full for fixture tests)\n");
    out("power-loss: external power cut required; this program cannot cut device power\n");
    log_line(logfd,"RixuriOS native test runner\nmode: safe native smoke\npower-loss: external power cut required\n");
    int failures=0;size_t count=sizeof(smoke)/sizeof(smoke[0]);
    for(size_t i=0;i<count;++i)failures+=run_one(&smoke[i],logfd)!=0;
    if(argc>1&&argv[1]&&argv[1][0]=='-'&&argv[1][1]=='-'&&argv[1][2]=='f'){
        out("full fixture mode\n");size_t full_count=sizeof(full)/sizeof(full[0]);
        for(size_t i=0;i<full_count;++i)failures+=run_one(&full[i],logfd)!=0;
    }else{
        skip_test(logfd,"credtest/auditcheck/metatest/renametest/proc-test","fixture or argument-sensitive; use --full");
    }
    if(argc>1&&argv[1]&&argv[1][0]=='-'&&argv[1][1]=='-'&&argv[1][2]=='p'){
        static const test_case_t pipe_test={"/usr/bin/pipe-stress"};out("pipe stress explicitly requested; runs last\n");failures+=run_one(&pipe_test,logfd)!=0;
    }else skip_test(logfd,"/usr/bin/pipe-stress","known kernel fault risk; use --pipe-stress");
    out("power-loss: manually cut power during accountctl add/rotate/remove, reboot, then run authcheck\n");out("summary: ");
    if(failures==0)out("\033[1;32mPASS\033[0m\n");else{out("\033[1;31mFAIL\033[0m failures=");out_num((uint64_t)failures);out("\n");}
    log_line(logfd,failures==0?"SUMMARY PASS\n":"SUMMARY FAIL\n");if(logfd>=0)(void)close(logfd);return failures==0?0:1;
}
