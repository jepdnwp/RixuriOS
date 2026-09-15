#include "unistd.h"
#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#define RIX_VFS_AT_FDCWD (-100)
#define RIX_VFS_O_WRONLY 1u
#define RIX_VFS_O_CREAT 4u
#define RIX_VFS_O_TRUNC 8u
#define RIX_WNOHANG 1u
#define RIX_SIGKILL 9u

/* Native on-device test runner. Every entry below runs with fixed
 * arguments and must exit 0 on success (bare-run safe, no fixtures
 * outside disposable /phase20-*, /usr/meta-*, /usr/rename-* paths and no
 * account-store mutation). Mutating tools (accountctl) are deliberately
 * NOT listed. Destructive fuzz numbers stay skipped inside fuzztest
 * itself by design (see its header comment). */
typedef struct { const char *path; const char *arg1; const char *arg2; } test_case_t;

static size_t length(const char *s) { size_t n=0; while(s&&s[n])++n; return n; }
static void out(const char *s) { (void)write(1,s,length(s)); }
static void log_line(int fd,const char *s) { if(fd>=0)(void)write(fd,s,length(s)); }
static void out_num(uint64_t v) { char b[24];size_t n=0;if(!v){out("0");return;}while(v&&n<sizeof(b)){b[n++]=(char)('0'+v%10u);v/=10u;}while(n){char c=b[--n];(void)write(1,&c,1);} }

static unsigned skip_count;

static int wait_with_timeout(rix_pid_t child,uint64_t *status,unsigned limit) {
    struct timespec pause={0,100000000};
    for(unsigned i=0;i<limit;++i){
        rix_pid_t r=waitpid(child,status,RIX_WNOHANG);
        if(r==child)return 0;
        if(r!=(rix_pid_t)0&&r!=(rix_pid_t)-1)return -1;
        (void)nanosleep(&pause,NULL);
    }
    (void)kill(child,RIX_SIGKILL);(void)waitpid(child,status,0);return 1;
}

static int run_one(const test_case_t *test,int logfd,unsigned limit) {
    char *argv[5];int n=0;
    argv[n++]=(char *)test->path;
    if(test->arg1)argv[n++]=(char *)test->arg1;
    if(test->arg2)argv[n++]=(char *)test->arg2;
    argv[n]=NULL;
    char *envp[]={(char *)"PATH=/bin:/usr/bin:/sbin:/usr/sbin",(char *)"PWD=/",NULL};
    rix_pid_t child=fork();if(child==(rix_pid_t)-1)return -1;
    if(child==0){
        if(execve(test->path,argv,envp)!=0){
            int e=errno;
            out("execve errno=");out_num((uint64_t)e);out(" ");
            _exit(127);
        }
        _exit(127);
    }
    uint64_t status=0;int result=wait_with_timeout(child,&status,limit);
    if(result==1){out("\033[1;31mTIMEOUT\033[0m ");out(test->path);out("\n");log_line(logfd,"TIMEOUT ");log_line(logfd,test->path);log_line(logfd,"\n");return 1;}
    if(result!=0||status!=0){out("\033[1;31mFAIL\033[0m ");out(test->path);out(" status=");out_num(status);out("\n");log_line(logfd,"FAIL ");log_line(logfd,test->path);log_line(logfd,"\n");return 1;}
    out("\033[1;32mPASS\033[0m ");out(test->path);
    if(test->arg1){out(" ");out(test->arg1);}
    if(test->arg2){out(" ");out(test->arg2);}
    out("\n");log_line(logfd,"PASS ");log_line(logfd,test->path);log_line(logfd,"\n");return 0;
}

static void skip_test(int logfd,const char *name,const char *why){
    out("SKIP ");out(name);out(" (");out(why);out(")\n");log_line(logfd,"SKIP ");log_line(logfd,name);log_line(logfd," ");log_line(logfd,why);log_line(logfd,"\n");
    ++skip_count;
}

static int has_flag(int argc,char **argv,const char *flag){
    size_t n=0;while(flag[n])++n;
    for(int i=1;i<argc;++i){
        size_t j=0;while(j<n&&argv[i][j]==flag[j])++j;
        if(j==n&&argv[i][n]==0)return 1;
    }
    return 0;
}

static int run_group(const test_case_t *group,size_t count,int logfd,unsigned limit){
    int failures=0;
    for(size_t i=0;i<count;++i)failures+=run_one(&group[i],logfd,limit)!=0;
    return failures;
}

static int live_processes(void){
    static rix_process_info_t table[128];
    size_t count=0;
    if(list_processes(table,128u,&count)!=0)return -1;
    return (int)count;
}

/* Task/process retirement gate: no test may leak live processes (zombies or
 * orphans). A delta across a group is a lifecycle bug, counted as failure. */
static int run_group_checked(const test_case_t *group,size_t count,int logfd,unsigned limit,const char *name){
    int before=live_processes();
    int failures=run_group(group,count,logfd,limit);
    int after=live_processes();
    if(before>=0&&after>=0&&before!=after){
        out("LEAK ");out(name);out(" procs=");out_num((uint64_t)before);out("->");out_num((uint64_t)after);out("\n");
        log_line(logfd,"LEAK ");log_line(logfd,name);log_line(logfd,"\n");
        failures++;
    }
    return failures;
}

/* Best-effort fixture teardown so --full stays re-runnable on one image.
 * Paths mirror exactly what credtest/metatest create; renametest cleans
 * itself. Errors ignored (teardown must not mask test results). */
static void cleanup_fixtures(void){
    (void)unlink("/phase20-work/acl-user");
    (void)unlink("/phase20-work/acl-group");
    (void)unlink("/phase20-work/acl-clear");
    (void)unlink("/phase20-work/owner");
    (void)unlink("/phase20-group");
    (void)unlink("/phase20-other");
    (void)unlink("/phase20-mode/secret");
    (void)unlink("/usr/meta-source");
    (void)unlink("/usr/meta-policy");
    (void)rmdir("/phase20-work");
    (void)rmdir("/phase20-mode");
    (void)rmdir("/usr/phase20-group-dir");
}

int program_main(int argc,char **argv,char **envp) {
    (void)envp;
    /* Safe smoke: bounded ABI/credential/session/pipe/ELF/diagnostic checks. */
    static const test_case_t smoke[]={
        {"/usr/bin/abi-negative",NULL,NULL},{"/usr/bin/capdelegatecheck",NULL,NULL},
        {"/usr/bin/capdelegatetest",NULL,NULL},{"/usr/bin/sessiontest",NULL,NULL},
        {"/usr/bin/sessionlisttest",NULL,NULL},{"/usr/bin/killtest",NULL,NULL},
        {"/usr/bin/cloexec-test",NULL,NULL},{"/usr/bin/pipetest",NULL,NULL},
        {"/usr/bin/posix-test",NULL,NULL},{"/usr/bin/threads",NULL,NULL},
        {"/bin/df",NULL,NULL},{"/usr/bin/free",NULL,NULL},
        {"/bin/dmesg",NULL,NULL},{"/usr/bin/ps",NULL,NULL},
        {"/usr/bin/uname",NULL,NULL}
    };
    /* Fixture tests using disposable /phase20-*, /usr/meta-*, /usr/rename-*.
     * NOTE: bare auditcheck always fails (needs audit_uid 4242, only set up
     * by credtest) and bare metatest always returns 2 (subcommand tool), so
     * they run here only in their runnable forms. */
    static const test_case_t full[]={
        {"/usr/bin/credtest",NULL,NULL},
        {"/usr/bin/metatest","init",NULL},
        {"/usr/bin/metatest","policy",NULL},
        {"/usr/bin/renametest",NULL,NULL},
        {"/usr/bin/proc-test",NULL,NULL}
    };
    static const test_case_t sched[]={
        {"/usr/bin/schedtest","burst",NULL},{"/usr/bin/schedtest","churn","10"}
    };
    static const test_case_t crash[]={
        {"/usr/bin/crashtest","null",NULL},{"/usr/bin/crashtest","ud",NULL}
    };
    static const test_case_t fuzz[]={
        {"/usr/bin/fuzztest",NULL,NULL}
    };
    static const test_case_t pipe_stress[]={
        {"/usr/bin/pipe-stress",NULL,NULL}
    };
    int logfd=openat(RIX_VFS_AT_FDCWD,"/usr/rixtest.log",RIX_VFS_O_WRONLY|RIX_VFS_O_CREAT|RIX_VFS_O_TRUNC,0644u);
    out("\033[1;36mRixuriOS native test runner\033[0m\n");out("log: /usr/rixtest.log\n");
    out("mode: safe native smoke (flags: --full --sched --crash --fuzz --pipe-stress --all --strict)\n");
    out("power-loss: external power cut required; this program cannot cut device power\n");
    log_line(logfd,"RixuriOS native test runner\npower-loss: external power cut required\n");
    int all=has_flag(argc,argv,"--all");
    int failures=0;
    failures+=run_group_checked(smoke,sizeof(smoke)/sizeof(smoke[0]),logfd,100u,"smoke");
    if(all||has_flag(argc,argv,"--full")){
        out("full fixture mode\n");
        failures+=run_group_checked(full,sizeof(full)/sizeof(full[0]),logfd,100u,"full");
        cleanup_fixtures();
    }else skip_test(logfd,"credtest/metatest/renametest/proc-test","fixture tests; use --full or --all");
    if(all||has_flag(argc,argv,"--sched")){
        out("scheduler mode (burst + churn 10)\n");
        failures+=run_group_checked(sched,sizeof(sched)/sizeof(sched[0]),logfd,200u,"sched");
    }else skip_test(logfd,"schedtest burst/churn","scheduler churn; use --sched or --all");
    if(all||has_flag(argc,argv,"--crash")){
        out("fault-kill mode\n");
        failures+=run_group_checked(crash,sizeof(crash)/sizeof(crash[0]),logfd,100u,"crash");
    }else skip_test(logfd,"crashtest null/ud","fault kill; use --crash or --all");
    if(all||has_flag(argc,argv,"--fuzz")){
        out("fuzz mode (6000 syscalls, slow)\n");
        failures+=run_group_checked(fuzz,sizeof(fuzz)/sizeof(fuzz[0]),logfd,1200u,"fuzz");
    }else skip_test(logfd,"/usr/bin/fuzztest","slow fuzz; use --fuzz or --all");
    if(all||has_flag(argc,argv,"--pipe-stress")){
        out("pipe stress explicitly requested; runs last\n");
        failures+=run_group_checked(pipe_stress,sizeof(pipe_stress)/sizeof(pipe_stress[0]),logfd,600u,"pipe-stress");
    }else skip_test(logfd,"/usr/bin/pipe-stress","stress timing; use --pipe-stress or --all");
    out("power-loss: manually cut power during accountctl add/rotate/remove, reboot, then run authcheck\n");out("summary: ");
    int strict=has_flag(argc,argv,"--strict");
    if(failures==0&&skip_count==0){out("\033[1;32mPASS\033[0m\n");log_line(logfd,"SUMMARY PASS\n");}
    else if(failures==0){out("\033[1;33mPASS WITH SKIPS\033[0m skipped=");out_num(skip_count);out(" (not phase evidence; re-run with --all, or --strict to fail)\n");log_line(logfd,"SUMMARY PASS WITH SKIPS\n");}
    else{out("\033[1;31mFAIL\033[0m failures=");out_num((uint64_t)failures);out("\n");log_line(logfd,"SUMMARY FAIL\n");}
    if(logfd>=0)(void)close(logfd);
    if(failures!=0)return 1;
    if(strict&&skip_count!=0)return 1;
    return 0;
}
