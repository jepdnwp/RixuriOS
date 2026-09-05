#include "process.h"
#include <stddef.h>

static rix_process_t table[RIX_PROCESS_MAX];
static pid_t current_pid;
static pid_t next_pid;
static size_t live_count;

static size_t bounded_strlen(const char *s){size_t n=0;if(!s)return 0;while(n<RIX_PROCESS_NAME_MAX-1&&s[n])n++;return n;}
static void copy_name(char *dst,const char *src){size_t n=bounded_strlen(src);for(size_t i=0;i<n;i++)dst[i]=src[i];dst[n]=0;}

int process_init(void){
    for(size_t i=0;i<RIX_PROCESS_MAX;i++){table[i].pid=0;table[i].parent=0;table[i].state=RIX_PROC_UNUSED;table[i].uid=0;table[i].gid=0;table[i].address_space=0;table[i].kernel_stack=0;table[i].exit_status=0;table[i].fd_bitmap=0;table[i].name[0]=0;}
    current_pid=0;next_pid=1;live_count=0;
    table[0].pid=0;table[0].state=RIX_PROC_RUNNING;table[0].uid=0;table[0].gid=0;copy_name(table[0].name,"kernel");live_count=1;
    return 0;
}

pid_t process_current(void){return current_pid;}
rix_process_t *process_lookup(pid_t pid){for(size_t i=0;i<RIX_PROCESS_MAX;i++)if(table[i].state!=RIX_PROC_UNUSED&&table[i].pid==pid)return &table[i];return NULL;}
size_t process_count(void){return live_count;}

int process_create(const char *name,pid_t parent,pid_t *out_pid){
    if(!name||!name[0])return -1;
    if(parent!=0&& !process_lookup(parent))return -1;
    for(size_t i=1;i<RIX_PROCESS_MAX;i++)if(table[i].state==RIX_PROC_UNUSED){
        pid_t pid=next_pid++;if(!pid)pid=next_pid++;
        table[i].pid=pid;table[i].parent=parent;table[i].state=RIX_PROC_SLEEPING;table[i].uid=0;table[i].gid=0;table[i].address_space=0;table[i].kernel_stack=0;table[i].exit_status=0;table[i].fd_bitmap=0;copy_name(table[i].name,name);live_count++;if(out_pid)*out_pid=pid;return 0;
    }
    return -1;
}

int process_set_state(pid_t pid,rix_process_state_t state){
    rix_process_t *p=process_lookup(pid);if(!p||state==RIX_PROC_UNUSED)return -1;
    p->state=state;if(state==RIX_PROC_RUNNING)current_pid=pid;return 0;
}

int process_exit(pid_t pid,uint64_t status){
    rix_process_t *p=process_lookup(pid);if(!p||pid==0)return -1;
    p->exit_status=status;p->state=RIX_PROC_ZOMBIE;if(current_pid==pid)current_pid=0;return 0;
}
