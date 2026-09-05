#include "signal.h"
#include "../sched/scheduler.h"

static int valid_signal(unsigned signal){return signal>=RIX_SIG_MIN&&signal<=RIX_SIG_MAX;}
static uint64_t signal_bit(unsigned signal){return 1ULL<<(signal-1u);}

int process_signal_send(pid_t pid,unsigned signal){
    if(!valid_signal(signal))return -1;
    rix_process_t *p=process_lookup(pid);
    if(!p||pid==0||p->state==RIX_PROC_UNUSED||p->state==RIX_PROC_ZOMBIE)return -1;
    p->signal_pending|=signal_bit(signal);
    if(p->state==RIX_PROC_SLEEPING && (p->signal_mask&signal_bit(signal))==0)p->state=RIX_PROC_RUNNING;
    return 0;
}

int process_signal_mask(pid_t pid,uint64_t mask){
    rix_process_t *p=process_lookup(pid);
    if(!p||p->state==RIX_PROC_UNUSED||p->state==RIX_PROC_ZOMBIE)return -1;
    mask&=~(signal_bit(RIX_SIGKILL)|signal_bit(RIX_SIGSTOP));
    p->signal_mask=mask;
    return 0;
}

int process_signal_pending(pid_t pid,uint64_t *pending){
    rix_process_t *p=process_lookup(pid);
    if(!p||!pending)return -1;
    *pending=p->signal_pending&~p->signal_mask;
    return 0;
}

int process_signal_take(pid_t pid,unsigned *signal){
    rix_process_t *p=process_lookup(pid);
    if(!p||!signal)return -1;
    uint64_t ready=p->signal_pending&~p->signal_mask;
    if(!ready)return 1;
    unsigned bit=(unsigned)__builtin_ctzll(ready);
    p->signal_pending&=~(1ULL<<bit);
    *signal=bit+1u;
    return 0;
}
