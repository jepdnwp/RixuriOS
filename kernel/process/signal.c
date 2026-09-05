#include "signal.h"

static int valid_signal(unsigned signal){return signal>=RIX_SIG_MIN&&signal<=RIX_SIG_MAX;}

int process_signal_send(pid_t pid,unsigned signal){
    if(!valid_signal(signal))return -1;
    rix_process_t *p=process_lookup(pid);
    if(!p||pid==0||p->state==RIX_PROC_UNUSED||p->state==RIX_PROC_ZOMBIE)return -1;
    p->signal_pending|=1ULL<<(signal-1u);
    return 0;
}

int process_signal_mask(pid_t pid,uint64_t mask){
    rix_process_t *p=process_lookup(pid);
    if(!p||p->state==RIX_PROC_UNUSED||p->state==RIX_PROC_ZOMBIE)return -1;
    /* SIGKILL and SIGSTOP cannot be blocked. */
    mask&=~((1ULL<<(RIX_SIGKILL-1u))|(1ULL<<(RIX_SIGSTOP-1u)));
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
