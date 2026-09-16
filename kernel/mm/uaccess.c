#include "uaccess.h"
#include "vmm.h"
#include "../arch/x86_64/idt.h"
#include "../arch/x86_64/smp.h"
#include "../sched/scheduler.h"
#include <stddef.h>
#include <stdint.h>

#define USER_MAX48 ((1ULL<<47)-1ULL)
#define RIX_EFAULT 14

/* Phase F1 per-CPU recovery record. Armed only across the raw copy
 * itself with preemption disabled (no migration, no sleep inside), so
 * at most one armed record exists per CPU and it always belongs to the
 * task faulting on that CPU. Single-consume: the fixup disarms, so a
 * second fault (e.g. inside the fixup path, impossible by construction
 * but defense in depth) falls through to the freeze path. */
typedef struct { volatile uint64_t armed; } uaccess_recovery_t;
static uaccess_recovery_t uaccess_recovery[SMP_MAX_CPUS];

static uint32_t uaccess_cpu(void){
    int id=smp_cpu_id();
    if(id>=0&&id<SMP_MAX_CPUS)return (uint32_t)id;
    int b=smp_bsp_index();
    if(b>=0&&b<SMP_MAX_CPUS)return (uint32_t)b;
    return 0;
}

static int range_ok(uint64_t address,size_t length){if(length==0)return 1;if(address>USER_MAX48)return 0;uint64_t last=address+(uint64_t)length-1ULL;if(last<address||last>USER_MAX48)return 0;return 1;}
int user_range_valid(uint64_t address,size_t length,int write){if(!range_ok(address,length))return -1;uint64_t end=address+(length?length-1:0);for(uint64_t page=address&~0xFFFULL;;page+=0x1000ULL){uint64_t flags=vmm_query_flags(page);if(!(flags&RIXURI_PTE_PRESENT)||(flags&RIXURI_PTE_USER)==0||(write&&!(flags&RIXURI_PTE_WRITE)))return -1;if(page>=(end&~0xFFFULL))break;}return 0;}

/* #PF dispatch hook (called from x86_exception_dispatch before any
 * forensics). Resumes kernel #PFs inside an armed uaccess bracket at
 * the matching fixup label. Returns 1 when consumed. Anything else —
 * wrong vector/CS, disarmed record, RIP outside both brackets (notably
 * the kernel-side access of the same loops) — returns 0 for the normal
 * freeze path. CPL3 faults never reach here as kernel faults (their CS
 * is 0x1b); user-fault killing is a separate dispatch step. */
int uaccess_fixup_frame(struct x86_fault_frame *frame){
    uint32_t me;
    if(!frame||frame->vector!=14||frame->cs!=0x08u)return 0;
    me=uaccess_cpu();
    if(!uaccess_recovery[me].armed)return 0;
    if(frame->rip>=(uint64_t)uaccess_from_fault_begin&&frame->rip<(uint64_t)uaccess_from_fault_end){
        uaccess_recovery[me].armed=0;
        frame->rip=(uint64_t)uaccess_from_fixup;
        return 1;
    }
    if(frame->rip>=(uint64_t)uaccess_to_fault_begin&&frame->rip<(uint64_t)uaccess_to_fault_end){
        uaccess_recovery[me].armed=0;
        frame->rip=(uint64_t)uaccess_to_fixup;
        return 1;
    }
    return 0;
}

int copy_from_user(void *kernel_dst,uint64_t user_src,size_t length){
    uint32_t me;
    int rc;
    if(!length)return 0;
    if(!kernel_dst||user_range_valid(user_src,length,0)!=0)return -RIX_EFAULT;
    me=uaccess_cpu();
    scheduler_preempt_disable();
    uaccess_recovery[me].armed=1;
    rc=uaccess_copy_from_user_raw(kernel_dst,user_src,length);
    uaccess_recovery[me].armed=0;
    scheduler_preempt_enable();
    return rc;
}
int copy_to_user(uint64_t user_dst,const void *kernel_src,size_t length){
    uint32_t me;
    int rc;
    if(!length)return 0;
    if(!kernel_src||user_range_valid(user_dst,length,1)!=0)return -RIX_EFAULT;
    me=uaccess_cpu();
    scheduler_preempt_disable();
    uaccess_recovery[me].armed=1;
    rc=uaccess_copy_to_user_raw(user_dst,kernel_src,length);
    uaccess_recovery[me].armed=0;
    scheduler_preempt_enable();
    return rc;
}

int uaccess_fixup_selftest(void){
    /* Canonical-user probe candidates far from every real mapping
     * (image low, heap at 0x8001_000000+, stacks at top). The first
     * candidate with the PRESENT bit clear is faulted single-byte
     * through the ARMED raw path. A correct chain returns nonzero
     * (-EFAULT from the fixup); a zero return means the load somehow
     * succeeded and the probe is invalid. */
    static const uint64_t cands[]={0x0000100000000000ULL,0x0000200000000000ULL,0x0000300000000000ULL};
    uint8_t sink = 0;
    uint8_t kbuf[8] = {0};
    for(unsigned c=0;c<3u;c++){
        uint64_t va=cands[c];
        if(vmm_query_flags(va)&RIXURI_PTE_PRESENT)continue;
        {
            uint32_t me=uaccess_cpu();
            int rc;
            scheduler_preempt_disable();
            uaccess_recovery[me].armed=1;
            rc=uaccess_copy_from_user_raw(&sink,va,1);
            uaccess_recovery[me].armed=0;
            scheduler_preempt_enable();
            if(rc!=0)return 0;
            return -1;
        }
    }
    {
        /* No unmapped candidate (should be impossible): at least prove
         * the to-user direction against a kernel-valid/user-invalid
         * pair the same way. */
        uint32_t me=uaccess_cpu();
        int rc;
        scheduler_preempt_disable();
        uaccess_recovery[me].armed=1;
        rc=uaccess_copy_to_user_raw(cands[0],kbuf,1);
        uaccess_recovery[me].armed=0;
        scheduler_preempt_enable();
        (void)kbuf;
        return rc!=0?0:-1;
    }
}
