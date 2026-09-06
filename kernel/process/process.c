#include "process.h"
#include "../elf/loader.h"
#include "../mm/pmm.h"
#include "../mm/vmm.h"
#include "../arch/x86_64/tss.h"
#include "../vfs/vfs.h"
#include <stddef.h>
#define USER_STACK_TOP 0x00007FFFFFFFF000ULL
#define USER_STACK_PAGES 32ULL
#define USER_STACK_BASE (USER_STACK_TOP-USER_STACK_PAGES*4096ULL)
#define KERNEL_STACK_SIZE 4096ULL
#define RIX_AUXV_AT_NULL 0ULL
static rix_process_t table[RIX_PROCESS_MAX];static uint32_t supplementary_groups[RIX_PROCESS_MAX][RIX_PROCESS_GROUP_MAX];static uint32_t supplementary_counts[RIX_PROCESS_MAX];static uint32_t real_uids[RIX_PROCESS_MAX],saved_uids[RIX_PROCESS_MAX],real_gids[RIX_PROCESS_MAX],saved_gids[RIX_PROCESS_MAX];static pid_t current_pid;static pid_t next_pid;static size_t live_count;
static size_t bounded_strlen(const char*s){size_t n=0;if(!s)return 0;while(n<RIX_PROCESS_NAME_MAX-1&&s[n])n++;return n;}
static void copy_name(char*d,const char*s){size_t n=bounded_strlen(s);for(size_t i=0;i<n;i++)d[i]=s[i];d[n]=0;}
static int copy_cwd(char*d,const char*s){size_t n=0;if(!d||!s)return -1;while(n+1<RIX_PROCESS_CWD_MAX&&s[n]){d[n]=s[n];++n;}if(s[n])return -1;d[n]=0;return 0;}
static pid_t allocate_pid(void){for(size_t n=0;n<RIX_PROCESS_MAX-1;n++){pid_t candidate=next_pid;if(++next_pid>=RIX_PROCESS_MAX)next_pid=1;if(candidate&& !process_lookup(candidate))return candidate;}return 0;}
static void clear_process(rix_process_t*p){p->pid=0;p->parent=0;p->process_group=0;p->session=0;p->state=RIX_PROC_UNUSED;p->uid=0;p->gid=0;p->address_space.pml4_phys=0;p->kernel_stack=0;p->kernel_stack_size=0;p->exit_status=0;p->fd_bitmap=0;p->signal_pending=0;p->signal_mask=0;p->name[0]=0;p->cwd[0]='/';p->cwd[1]=0;}
static void zero_page(uint64_t pa){uint8_t*p=(uint8_t*)(uintptr_t)pa;for(size_t i=0;i<4096;i++)p[i]=0;}
int process_init(void){for(size_t i=0;i<RIX_PROCESS_MAX;i++){clear_process(&table[i]);supplementary_counts[i]=0;real_uids[i]=saved_uids[i]=real_gids[i]=saved_gids[i]=0;for(size_t g=0;g<RIX_PROCESS_GROUP_MAX;g++)supplementary_groups[i][g]=0;}current_pid=0;next_pid=1;live_count=1;table[0].pid=0;table[0].process_group=0;table[0].session=0;table[0].state=RIX_PROC_RUNNING;copy_name(table[0].name,"kernel");return 0;}
pid_t process_current(void){return current_pid;}rix_process_t*process_lookup(pid_t pid){for(size_t i=0;i<RIX_PROCESS_MAX;i++)if(table[i].state!=RIX_PROC_UNUSED&&table[i].pid==pid)return &table[i];return NULL;}size_t process_count(void){return live_count;}
int process_getcwd(pid_t pid,char*out,size_t capacity){rix_process_t*p=process_lookup(pid);if(!p||!out||capacity==0)return -1;size_t n=0;while(p->cwd[n]){if(n+1>=capacity)return -1;out[n]=p->cwd[n];++n;}out[n]=0;return 0;}
int process_setcwd(pid_t pid,const char*path){rix_process_t*p=process_lookup(pid);if(!p||!path||!path[0])return -1;return copy_cwd(p->cwd,path);}
uint32_t process_uid(pid_t pid){rix_process_t*p=process_lookup(pid);return p?p->uid:UINT32_MAX;}
uint32_t process_gid(pid_t pid){rix_process_t*p=process_lookup(pid);return p?p->gid:UINT32_MAX;}
int process_setuid(pid_t pid,uint32_t uid){rix_process_t*p=process_lookup(pid);size_t index=(size_t)pid;if(!p||index>=RIX_PROCESS_MAX)return -1;if(p->uid==0){real_uids[index]=uid;saved_uids[index]=uid;p->uid=uid;return 0;}if(uid==real_uids[index]||uid==saved_uids[index]){p->uid=uid;return 0;}return -1;}
int process_setgid(pid_t pid,uint32_t gid){rix_process_t*p=process_lookup(pid);size_t index=(size_t)pid;if(!p||index>=RIX_PROCESS_MAX)return -1;if(p->uid==0){real_gids[index]=gid;saved_gids[index]=gid;p->gid=gid;return 0;}if(gid==real_gids[index]||gid==saved_gids[index]){p->gid=gid;return 0;}return -1;}
int process_in_group(pid_t pid,uint32_t gid){rix_process_t*p=process_lookup(pid);if(!p)return 0;if(p->gid==gid)return 1;size_t index=(size_t)p->pid;if(index<RIX_PROCESS_MAX)for(size_t i=0;i<supplementary_counts[index];i++)if(supplementary_groups[index][i]==gid)return 1;return 0;}
int process_getgroups(pid_t pid,uint32_t*groups,size_t capacity,size_t*count){rix_process_t*p=process_lookup(pid);size_t index=(size_t)pid;if(!p||!count||index>=RIX_PROCESS_MAX||(capacity&&capacity<supplementary_counts[index])||(!groups&&capacity))return -1;if(groups&&capacity)for(size_t i=0;i<supplementary_counts[index];i++)groups[i]=supplementary_groups[index][i];*count=supplementary_counts[index];return 0;}
int process_setgroups(pid_t pid,const uint32_t*groups,size_t count){rix_process_t*p=process_lookup(pid);size_t index=(size_t)pid;if(!p||p->uid!=0||index>=RIX_PROCESS_MAX||count>RIX_PROCESS_GROUP_MAX||(count&&!groups))return -1;for(size_t i=0;i<count;i++)supplementary_groups[index][i]=groups[i];supplementary_counts[index]=(uint32_t)count;return 0;}
int process_apply_exec_credentials(pid_t pid,uint32_t uid,uint32_t gid,int setuid_bit,int setgid_bit){rix_process_t*p=process_lookup(pid);size_t index=(size_t)pid;if(!p||index>=RIX_PROCESS_MAX)return -1;if(setuid_bit){p->uid=uid;saved_uids[index]=uid;}if(setgid_bit){p->gid=gid;saved_gids[index]=gid;}return 0;}
int process_create(const char*name,pid_t parent,pid_t*out_pid){if(!name||!name[0]||(parent&& !process_lookup(parent)))return -1;for(size_t i=1;i<RIX_PROCESS_MAX;i++)if(table[i].state==RIX_PROC_UNUSED){pid_t pid=allocate_pid();if(!pid)return -1;clear_process(&table[i]);supplementary_counts[i]=0;real_uids[i]=saved_uids[i]=real_gids[i]=saved_gids[i]=0;table[i].pid=pid;table[i].parent=parent;rix_process_t*pp=parent?process_lookup(parent):NULL;table[i].process_group=pp?pp->process_group:pid;table[i].session=pp?pp->session:pid;if(pp){copy_cwd(table[i].cwd,pp->cwd);table[i].uid=pp->uid;table[i].gid=pp->gid;size_t parent_index=(size_t)parent;if(parent_index<RIX_PROCESS_MAX){real_uids[i]=real_uids[parent_index];saved_uids[i]=saved_uids[parent_index];real_gids[i]=real_gids[parent_index];saved_gids[i]=saved_gids[parent_index];supplementary_counts[i]=supplementary_counts[parent_index];for(size_t g=0;g<supplementary_counts[i];g++)supplementary_groups[i][g]=supplementary_groups[parent_index][g];}}table[i].state=RIX_PROC_SLEEPING;if(parent&&vfs_clone_fds(parent,pid)!=0){(void)vfs_close_all(pid);clear_process(&table[i]);return -1;}copy_name(table[i].name,name);live_count++;if(out_pid)*out_pid=pid;return 0;}return -1;}
int process_create_user(const char*name,pid_t parent,const void*image,uint64_t image_size,pid_t*out_pid,uint64_t*out_entry,uint64_t*out_user_stack){if(!name||!name[0]||!image||!image_size||!out_pid||!out_entry||!out_user_stack||(parent&&!process_lookup(parent)))return -1;pid_t pid;if(process_create(name,parent,&pid)!=0)return -1;rix_process_t*p=process_lookup(pid);if(!p)return -1;if(address_space_create(&p->address_space)!=0)goto fail;for(uint64_t i=0;i<USER_STACK_PAGES;i++){uint64_t pa=pmm_alloc_page();if(!pa)goto fail;zero_page(pa);if(address_space_map(&p->address_space,USER_STACK_BASE+i*4096ULL,pa,RIXURI_PTE_WRITE|RIXURI_PTE_USER|RIXURI_PTE_NX)!=0){pmm_free_page(pa);goto fail;}}rix_elf_image_t elf;if(elf_load_image(image,image_size,&p->address_space,&elf)!=0)goto fail;uint64_t ks=pmm_alloc_page();if(!ks)goto fail;zero_page(ks);p->kernel_stack=ks;p->kernel_stack_size=KERNEL_STACK_SIZE;p->state=RIX_PROC_SLEEPING;*out_pid=pid;*out_entry=elf.entry;*out_user_stack=USER_STACK_TOP;return 0;fail:(void)vfs_close_all(pid);address_space_destroy(&p->address_space);if(p->kernel_stack)pmm_free_page(p->kernel_stack);clear_process(p);if(live_count)live_count--;return -1;}
int process_fork(pid_t parent,uint64_t user_rip,uint64_t user_rsp,pid_t *out_pid){
    if (!out_pid || !user_rip || !user_rsp) return -1;
    rix_process_t *pp=process_lookup(parent);if(!pp||!pp->address_space.pml4_phys)return -1;
    pid_t child;if(process_create("fork-child",parent,&child)!=0)return -1;
    rix_process_t *cp=process_lookup(child);if(!cp)return -1;
    if(address_space_clone(&pp->address_space,&cp->address_space)!=0)goto fail;
    uint64_t ks=pmm_alloc_page();if(!ks)goto fail;
    zero_page(ks);cp->kernel_stack=ks;cp->kernel_stack_size=KERNEL_STACK_SIZE;cp->state=RIX_PROC_SLEEPING;*out_pid=child;return 0;
fail:
    (void)vfs_close_all(child);address_space_destroy(&cp->address_space);clear_process(cp);if(live_count)live_count--;return -1;
}
static int stack_write(const rix_address_space_t *as, uint64_t address,
                        const void *source, size_t length) {
    if (!as || (!source && length) || address < USER_STACK_BASE ||
        length > USER_STACK_TOP - address) return -1;
    const uint8_t *src = (const uint8_t *)source;
    for (size_t i = 0; i < length; ++i) {
        uint64_t pa = address_space_translate(as, address + i);
        if (!pa) return -1;
        *(volatile uint8_t *)(uintptr_t)pa = src[i];
    }
    return 0;
}

static int stack_write_u64(const rix_address_space_t *as, uint64_t address, uint64_t value) {
    return stack_write(as, address, &value, sizeof(value));
}

static int stack_copy_string(const rix_address_space_t *as, uint64_t *sp,
                             const char *source, uint64_t *user_address) {
    if (!as || !sp || !source || !user_address) return -1;
    size_t length = 0;
    while (length < RIX_PROCESS_ARG_TEXT_MAX && source[length]) ++length;
    if (length >= RIX_PROCESS_ARG_TEXT_MAX || *sp < USER_STACK_BASE + length + 1u)
        return -1;
    *sp -= length + 1u;
    if (stack_write(as, *sp, source, length) != 0 ||
        stack_write(as, *sp + length, "", 1u) != 0) return -1;
    *user_address = *sp;
    return 0;
}

static int stack_build_args(const rix_address_space_t *as, uint64_t *out_sp,
                            const char *const *argv, size_t argc,
                            const char *const *envp, size_t envc) {
    if (!as || !out_sp || argc > RIX_PROCESS_ARG_MAX ||
        envc > RIX_PROCESS_ARG_MAX || (argc && !argv) || (envc && !envp)) return -1;
    uint64_t arg_address[RIX_PROCESS_ARG_MAX];
    uint64_t env_address[RIX_PROCESS_ARG_MAX];
    uint64_t sp = USER_STACK_TOP;
    for (size_t i = argc; i-- > 0u;) {
        if (stack_copy_string(as, &sp, argv[i], &arg_address[i]) != 0) return -1;
    }
    for (size_t i = envc; i-- > 0u;) {
        if (stack_copy_string(as, &sp, envp[i], &env_address[i]) != 0) return -1;
    }
    sp &= ~0xFULL;
    size_t vector_words = argc + envc + 5u;
    if (sp < USER_STACK_BASE ||
        vector_words + (vector_words & 1u) >
        (sp - USER_STACK_BASE) / sizeof(uint64_t)) return -1;
    if (vector_words & 1u) {
        sp -= sizeof(uint64_t);
        if (stack_write_u64(as, sp, 0) != 0) return -1;
    }
    sp -= sizeof(uint64_t);
    if (stack_write_u64(as, sp, RIX_AUXV_AT_NULL) != 0) return -1;
    sp -= sizeof(uint64_t);
    if (stack_write_u64(as, sp, RIX_AUXV_AT_NULL) != 0) return -1;
    sp -= sizeof(uint64_t);
    if (stack_write_u64(as, sp, 0) != 0) return -1;
    for (size_t i = envc; i-- > 0u;) {
        sp -= sizeof(uint64_t);
        if (stack_write_u64(as, sp, env_address[i]) != 0) return -1;
    }
    sp -= sizeof(uint64_t);
    if (stack_write_u64(as, sp, 0) != 0) return -1;
    for (size_t i = argc; i-- > 0u;) {
        sp -= sizeof(uint64_t);
        if (stack_write_u64(as, sp, arg_address[i]) != 0) return -1;
    }
    sp -= sizeof(uint64_t);
    if (stack_write_u64(as, sp, argc) != 0) return -1;
    *out_sp = sp;
    return 0;
}

int process_exec_user_with_args(pid_t pid, const void *image, uint64_t image_size,
                                const char *const *argv, size_t argc,
                                const char *const *envp, size_t envc,
                                uint64_t *out_entry, uint64_t *out_user_stack) {
    if (!image || !image_size || !out_entry || !out_user_stack ||
        argc > RIX_PROCESS_ARG_MAX || envc > RIX_PROCESS_ARG_MAX ||
        (argc && !argv) ||
        (envc && !envp)) return -1;
    rix_process_t *p = process_lookup(pid);
    if (!p || !p->address_space.pml4_phys) return -1;
    rix_address_space_t replacement = {0};
    if (address_space_create(&replacement) != 0) return -1;
    for (uint64_t i = 0; i < USER_STACK_PAGES; ++i) {
        uint64_t pa = pmm_alloc_page();
        if (!pa) goto fail;
        zero_page(pa);
        if (address_space_map(&replacement, USER_STACK_BASE + i * 4096ULL, pa,
                              RIXURI_PTE_WRITE | RIXURI_PTE_USER | RIXURI_PTE_NX) != 0) {
            pmm_free_page(pa);
            goto fail;
        }
    }
    rix_elf_image_t elf;
    if (elf_load_image(image, image_size, &replacement, &elf) != 0) goto fail;
    uint64_t user_stack = USER_STACK_TOP;
    if (stack_build_args(&replacement, &user_stack, argv, argc, envp, envc) != 0) goto fail;
    address_space_destroy(&p->address_space);
    p->address_space = replacement;
    *out_entry = elf.entry;
    *out_user_stack = user_stack;
    return 0;
fail:
    address_space_destroy(&replacement);
    return -1;
}

int process_exec_user(pid_t pid, const void *image, uint64_t image_size,
                      uint64_t *out_entry, uint64_t *out_user_stack) {
    const char *argv[] = { "program" };
    return process_exec_user_with_args(pid, image, image_size, argv, 1u,
                                       NULL, 0u, out_entry, out_user_stack);
}

int process_activate(pid_t pid){if(pid==0){current_pid=0;vmm_switch_pml4(vmm_kernel_pml4());return 0;}rix_process_t*p=process_lookup(pid);if(!p||p->state==RIX_PROC_UNUSED||p->state==RIX_PROC_ZOMBIE||!p->address_space.pml4_phys||!p->kernel_stack)return -1;current_pid=pid;tss_set_rsp0(p->kernel_stack+p->kernel_stack_size);vmm_switch_pml4(p->address_space.pml4_phys);return 0;}
int process_set_state(pid_t pid,rix_process_state_t state){rix_process_t*p=process_lookup(pid);if(!p||state==RIX_PROC_UNUSED)return -1;rix_process_state_t old=p->state;p->state=state;if(state==RIX_PROC_RUNNING&&process_activate(pid)!=0){p->state=old;return -1;}return 0;}
int process_exit(pid_t pid,uint64_t status){rix_process_t*p=process_lookup(pid);if(!p||pid==0||p->state==RIX_PROC_ZOMBIE)return -1;(void)vfs_close_all(pid);p->exit_status=status;p->state=RIX_PROC_ZOMBIE;if(current_pid==pid)current_pid=0;return 0;}
int process_set_group(pid_t pid,pid_t process_group){rix_process_t*p=process_lookup(pid);if(!p||!process_group)return -1;p->process_group=process_group;return 0;}
int process_set_session(pid_t pid,pid_t session){rix_process_t*p=process_lookup(pid);if(!p||!session)return -1;p->session=session;return 0;}
int process_signal_group(pid_t process_group,unsigned signal){if(!process_group||signal<1u||signal>64u)return -1;uint64_t bit=1ULL<<(signal-1u);int sent=0;for(size_t i=0;i<RIX_PROCESS_MAX;i++){rix_process_t*p=&table[i];if(p->state!=RIX_PROC_UNUSED&&p->state!=RIX_PROC_ZOMBIE&&p->process_group==process_group){p->signal_pending|=bit;if(p->state==RIX_PROC_SLEEPING&&(p->signal_mask&bit)==0)p->state=RIX_PROC_RUNNING;sent++;}}return sent?0:-1;}
int process_wait(pid_t parent,pid_t wanted,uint64_t*status,pid_t*child_pid){if(!status||!child_pid)return -1;rix_process_t*match=NULL;int has_child=0;for(size_t i=1;i<RIX_PROCESS_MAX;i++){rix_process_t*p=&table[i];if(p->state!=RIX_PROC_UNUSED&&p->parent==parent&&(wanted==(pid_t)-1||p->pid==wanted)){has_child=1;if(p->state==RIX_PROC_ZOMBIE){match=p;break;}}}if(!has_child)return 2;if(!match)return 1;*status=match->exit_status;*child_pid=match->pid;(void)vfs_close_all(match->pid);address_space_destroy(&match->address_space);if(match->kernel_stack)pmm_free_page(match->kernel_stack);clear_process(match);if(live_count)live_count--;return 0;}
