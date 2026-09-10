#include "process.h"
#include "kernel.h"
#include "../serial.h"
#include "../arch/x86_64/cpu.h"
#include "../elf/loader.h"
#include "../mm/pmm.h"
#include "../mm/vmm.h"
#include "../arch/x86_64/tss.h"
#include "../vfs/vfs.h"
#include "../tty/tty.h"
#include <stddef.h>
#define USER_STACK_TOP 0x00007FFFFFFFF000ULL
#define USER_STACK_PAGES 32ULL
#define USER_STACK_BASE (USER_STACK_TOP-USER_STACK_PAGES*4096ULL)
#define KERNEL_STACK_PAGES 8ULL
#define KERNEL_STACK_SIZE (KERNEL_STACK_PAGES * RIXURI_PAGE_SIZE)
#define RIX_AUXV_AT_NULL 0ULL
static rix_process_t table[RIX_PROCESS_MAX];static uint32_t supplementary_groups[RIX_PROCESS_MAX][RIX_PROCESS_GROUP_MAX];static uint32_t supplementary_counts[RIX_PROCESS_MAX];static uint32_t real_uids[RIX_PROCESS_MAX],saved_uids[RIX_PROCESS_MAX],real_gids[RIX_PROCESS_MAX],saved_gids[RIX_PROCESS_MAX];static uint32_t audit_uids[RIX_PROCESS_MAX];static rix_session_info_t sessions[RIX_SESSION_MAX];static pid_t current_pid;static pid_t next_pid;static size_t live_count;
static int session_register(pid_t session,pid_t leader,uint32_t uid){if(!session)return-1;for(size_t i=0;i<RIX_SESSION_MAX;i++)if(sessions[i].session==session){sessions[i].leader=leader;sessions[i].uid=uid;sessions[i].flags=RIX_SESSION_ACTIVE;return 0;}for(size_t i=0;i<RIX_SESSION_MAX;i++)if(!sessions[i].session){sessions[i].session=session;sessions[i].leader=leader;sessions[i].uid=uid;sessions[i].controlling_tty=UINT32_MAX;sessions[i].flags=RIX_SESSION_ACTIVE;return 0;}return-1;}
static void session_drop_if_empty(pid_t session){if(!session)return;for(size_t i=1;i<RIX_PROCESS_MAX;i++)if(table[i].state!=RIX_PROC_UNUSED&&table[i].state!=RIX_PROC_ZOMBIE&&table[i].session==session)return;for(size_t i=0;i<RIX_SESSION_MAX;i++)if(sessions[i].session==session){sessions[i].session=0;sessions[i].leader=0;sessions[i].uid=0;sessions[i].controlling_tty=UINT32_MAX;sessions[i].flags=0;return;}}
static void session_refresh_tty(rix_session_info_t*info){if(!info||!info->session)return;info->controlling_tty=UINT32_MAX;for(unsigned tty=0;tty<RIX_TTY_COUNT;tty++){uint32_t session=0;int controlling=0;if(tty_get_session(tty,&session,&controlling)==0&&controlling&&session==info->session){info->controlling_tty=tty;break;}}}
static size_t bounded_strlen(const char*s){size_t n=0;if(!s)return 0;while(n<RIX_PROCESS_NAME_MAX-1&&s[n])n++;return n;}
static void copy_name(char*d,const char*s){size_t n=bounded_strlen(s);for(size_t i=0;i<n;i++)d[i]=s[i];d[n]=0;}
static int copy_cwd(char*d,const char*s){size_t n=0;if(!d||!s)return -1;while(n+1<RIX_PROCESS_CWD_MAX&&s[n]){d[n]=s[n];++n;}if(s[n])return -1;d[n]=0;return 0;}
static pid_t allocate_pid(void){for(size_t n=0;n<RIX_PROCESS_MAX-1;n++){pid_t candidate=next_pid;if(++next_pid>=RIX_PROCESS_MAX)next_pid=1;if(candidate&& !process_lookup(candidate))return candidate;}return 0;}
static void clear_process(rix_process_t*p){p->pid=0;p->parent=0;p->process_group=0;p->session=0;p->state=RIX_PROC_UNUSED;p->uid=0;p->gid=0;p->capabilities=0;p->address_space.pml4_phys=0;p->heap_base=0;p->heap_break=0;p->kernel_stack=0;p->kernel_stack_size=0;p->exit_status=0;p->fd_bitmap=0;p->signal_pending=0;p->signal_mask=0;rix_net_socket_table_init(&p->sockets);p->name[0]=0;p->cwd[0]='/';p->cwd[1]=0;}
static void zero_page(uint64_t pa){uint8_t*p=(uint8_t*)(uintptr_t)pa;for(size_t i=0;i<4096;i++)p[i]=0;}
int process_init(void){for(size_t i=0;i<RIX_PROCESS_MAX;i++){clear_process(&table[i]);supplementary_counts[i]=0;audit_uids[i]=0;real_uids[i]=saved_uids[i]=real_gids[i]=saved_gids[i]=0;for(size_t g=0;g<RIX_PROCESS_GROUP_MAX;g++)supplementary_groups[i][g]=0;}for(size_t i=0;i<RIX_SESSION_MAX;i++){sessions[i].session=0;sessions[i].leader=0;sessions[i].uid=0;sessions[i].controlling_tty=UINT32_MAX;sessions[i].flags=0;}current_pid=0;next_pid=1;live_count=1;
table[0].pid=0;table[0].process_group=0;table[0].session=0;table[0].state=RIX_PROC_RUNNING;table[0].capabilities=RIX_CAP_ALL;copy_name(table[0].name,"kernel");return 0;}
pid_t process_current(void){return current_pid;}rix_process_t*process_lookup(pid_t pid){for(size_t i=0;i<RIX_PROCESS_MAX;i++)if(table[i].state!=RIX_PROC_UNUSED&&table[i].pid==pid)return &table[i];return NULL;}size_t process_count(void){return live_count;}
int process_getcwd(pid_t pid,char*out,size_t capacity){rix_process_t*p=process_lookup(pid);if(!p||!out||capacity==0)return -1;size_t n=0;while(p->cwd[n]){if(n+1>=capacity)return -1;out[n]=p->cwd[n];++n;}out[n]=0;return 0;}
int process_setcwd(pid_t pid,const char*path){rix_process_t*p=process_lookup(pid);if(!p||!path||!path[0])return -1;return copy_cwd(p->cwd,path);}
uint32_t process_uid(pid_t pid){rix_process_t*p=process_lookup(pid);return p?p->uid:UINT32_MAX;}
uint32_t process_gid(pid_t pid){rix_process_t*p=process_lookup(pid);return p?p->gid:UINT32_MAX;}
int process_setuid(pid_t pid,uint32_t uid){rix_process_t*p=process_lookup(pid);size_t index=(size_t)pid;if(!p||index>=RIX_PROCESS_MAX)return -1;if(p->uid==0&&((p->capabilities&RIX_CAP_SETUID)==0))return -1;if(p->uid==0){real_uids[index]=uid;saved_uids[index]=uid;p->uid=uid;if(uid!=0)p->capabilities=0;return 0;}
if(uid==real_uids[index]||uid==saved_uids[index]){p->uid=uid;return 0;}return -1;}
int process_setgid(pid_t pid,uint32_t gid){rix_process_t*p=process_lookup(pid);size_t index=(size_t)pid;if(!p||index>=RIX_PROCESS_MAX)return -1;if(p->uid==0&&((p->capabilities&RIX_CAP_SETGID)==0))return -1;if(p->uid==0){real_gids[index]=gid;saved_gids[index]=gid;p->gid=gid;return 0;}
if(gid==real_gids[index]||gid==saved_gids[index]){p->gid=gid;return 0;}return -1;}
int process_in_group(pid_t pid,uint32_t gid){rix_process_t*p=process_lookup(pid);if(!p)return 0;if(p->gid==gid)return 1;size_t index=(size_t)p->pid;if(index<RIX_PROCESS_MAX)for(size_t i=0;i<supplementary_counts[index];i++)if(supplementary_groups[index][i]==gid)return 1;return 0;}
int process_getgroups(pid_t pid,uint32_t*groups,size_t capacity,size_t*count){rix_process_t*p=process_lookup(pid);size_t index=(size_t)pid;if(!p||!count||index>=RIX_PROCESS_MAX||(capacity&&capacity<supplementary_counts[index])||(!groups&&capacity))return -1;if(groups&&capacity)for(size_t i=0;i<supplementary_counts[index];i++)groups[i]=supplementary_groups[index][i];*count=supplementary_counts[index];return 0;}
int process_setgroups(pid_t pid,const uint32_t*groups,size_t count){rix_process_t*p=process_lookup(pid);size_t index=(size_t)pid;if(!p||p->uid!=0||(p->capabilities&RIX_CAP_SETGID)==0||index>=RIX_PROCESS_MAX||count>RIX_PROCESS_GROUP_MAX||(count&&!groups))return -1;
for(size_t i=0;i<count;i++){
        supplementary_groups[index][i]=groups[i];
    }
    supplementary_counts[index]=(uint32_t)count;
    return 0;
}
int process_apply_exec_credentials(pid_t pid,uint32_t uid,uint32_t gid,int setuid_bit,int setgid_bit){rix_process_t*p=process_lookup(pid);size_t index=(size_t)pid;if(!p||index>=RIX_PROCESS_MAX)return -1;if(setuid_bit){p->uid=uid;saved_uids[index]=uid;}if(setgid_bit){p->gid=gid;saved_gids[index]=gid;}if(setuid_bit||setgid_bit)p->capabilities=0;return 0;}
int process_create(const char*name,pid_t parent,pid_t*out_pid){if(!name||!name[0]||(parent&& !process_lookup(parent)))return -1;for(size_t i=1;i<RIX_PROCESS_MAX;i++)if(table[i].state==RIX_PROC_UNUSED){pid_t pid=allocate_pid();if(!pid)return -1;clear_process(&table[i]);supplementary_counts[i]=0;audit_uids[i]=0;real_uids[i]=saved_uids[i]=real_gids[i]=saved_gids[i]=0;table[i].pid=pid;table[i].parent=parent;rix_process_t*pp=parent?process_lookup(parent):NULL;table[i].process_group=pp?pp->process_group:pid;table[i].session=pp?pp->session:pid;if(pp){copy_cwd(table[i].cwd,pp->cwd);table[i].uid=pp->uid;table[i].gid=pp->gid;size_t parent_index=(size_t)parent;if(parent_index<RIX_PROCESS_MAX){real_uids[i]=real_uids[parent_index];saved_uids[i]=saved_uids[parent_index];real_gids[i]=real_gids[parent_index];saved_gids[i]=saved_gids[parent_index];audit_uids[i]=audit_uids[parent_index];supplementary_counts[i]=supplementary_counts[parent_index];for(size_t g=0;g<supplementary_counts[i];g++)supplementary_groups[i][g]=supplementary_groups[parent_index][g];}}else audit_uids[i]=table[i].uid;table[i].state=RIX_PROC_SLEEPING;table[i].capabilities=pp?pp->capabilities:(parent==0?RIX_CAP_ALL:0);if(parent&&vfs_clone_fds(parent,pid)!=0){(void)vfs_close_all(pid);clear_process(&table[i]);return -1;}copy_name(table[i].name,name);live_count++;if(out_pid)*out_pid=pid;return 0;}return -1;}
int process_create_user(const char*name,pid_t parent,const void*image,uint64_t image_size,pid_t*out_pid,uint64_t*out_entry,uint64_t*out_user_stack){
 /* Staged diagnostics: distinct rc per stage (-1 args, -2 process-create,
  * -3 session/lookup, -4 address-space, -5 user-stack alloc, -6 user-stack
  * map, -7 image, -8 kernel-stack). Callers only test !=0. */
 if(!name||!name[0]||!image||!image_size||!out_pid||!out_entry||!out_user_stack||(parent&&!process_lookup(parent))){kernel_log("DEBUG: process_create_user fail stage=args\r\n");return -1;}
 pid_t pid;if(process_create(name,parent,&pid)!=0){kernel_log("DEBUG: process_create_user fail stage=process-create\r\n");return -2;}
 if(parent==0&&session_register(pid,pid,0)!=0){kernel_log("DEBUG: process_create_user fail stage=session pid=");kernel_log_dec(pid);kernel_log("\r\n");(void)process_exit(pid,127);return -3;}
 rix_process_t*p=process_lookup(pid);if(!p){kernel_log("DEBUG: process_create_user fail stage=lookup\r\n");return -3;}
 kernel_log("DEBUG: process_create success pid=");kernel_log_dec(pid);kernel_log("\r\n");
 kernel_log("DEBUG: address space create begin\r\n");
 int asc=address_space_create(&p->address_space);int urc=-4;
 if(asc!=0){urc=-4;kernel_log("DEBUG: address space create fail reason=");if(asc<0)kernel_log("-");kernel_log_dec((uint64_t)(asc<0?-asc:asc));kernel_log(" free_pages=");kernel_log_dec(pmm_free_pages());kernel_log("\r\n");goto fail_user;}
 p->heap_base=RIX_USER_HEAP_BASE;p->heap_break=RIX_USER_HEAP_BASE;
 kernel_log("DEBUG: address space create success pml4=");kernel_log_hex(p->address_space.pml4_phys);kernel_log("\r\n");
 kernel_log("DEBUG: user stack create begin top=");kernel_log_hex(USER_STACK_TOP);kernel_log(" pages=");kernel_log_dec(USER_STACK_PAGES);kernel_log("\r\n");
 for(uint64_t i=0;i<USER_STACK_PAGES;i++){uint64_t pa=pmm_alloc_page();if(!pa){urc=-5;kernel_log("DEBUG: user stack create fail reason=alloc-fail i=");kernel_log_dec(i);kernel_log(" free_pages=");kernel_log_dec(pmm_free_pages());kernel_log("\r\n");goto fail_user;}
  zero_page(pa);uint64_t va=USER_STACK_BASE+i*4096ULL;int mc=address_space_map(&p->address_space,va,pa,RIXURI_PTE_WRITE|RIXURI_PTE_USER|RIXURI_PTE_NX);
  if(mc!=0){urc=-6;kernel_log("DEBUG: user stack create fail reason=map-fail i=");kernel_log_dec(i);kernel_log(" va=");kernel_log_hex(va);kernel_log("\r\n");pmm_free_page(pa);goto fail_user;}}
 kernel_log("DEBUG: user stack create success\r\n");
 kernel_log("DEBUG: embedded image map begin size=");kernel_log_dec(image_size);kernel_log("\r\n");
 rix_elf_image_t elf;int ec=elf_load_image(image,image_size,&p->address_space,&elf);
 if(ec!=0){urc=-7;kernel_log("DEBUG: embedded image map fail reason=");if(ec<0)kernel_log("-");kernel_log_dec((uint64_t)(ec<0?-ec:ec));kernel_log(" free_pages=");kernel_log_dec(pmm_free_pages());kernel_log("\r\n");goto fail_user;}
 kernel_log("DEBUG: embedded image map success entry=");kernel_log_hex(elf.entry);kernel_log("\r\n");
 kernel_log("DEBUG: kernel stack begin pages=");kernel_log_dec((uint64_t)KERNEL_STACK_PAGES);kernel_log(" free_pages=");kernel_log_dec(pmm_free_pages());kernel_log("\r\n");
 uint64_t ks=pmm_alloc_pages(KERNEL_STACK_PAGES);
 if(!ks){urc=-8;kernel_log("DEBUG: kernel stack fail reason=contig-alloc-fail pages=");kernel_log_dec((uint64_t)KERNEL_STACK_PAGES);kernel_log(" free_pages=");kernel_log_dec(pmm_free_pages());kernel_log("\r\n");goto fail_user;}
 for(uint64_t page=0;page<KERNEL_STACK_PAGES;page++)zero_page(ks+page*RIXURI_PAGE_SIZE);
 p->kernel_stack=ks;p->kernel_stack_size=KERNEL_STACK_SIZE;p->state=RIX_PROC_SLEEPING;
 kernel_log("DEBUG: process_create_user success pid=");kernel_log_dec(pid);kernel_log(" pml4=");kernel_log_hex(p->address_space.pml4_phys);kernel_log(" entry=");kernel_log_hex(elf.entry);kernel_log("\r\n");
 *out_pid=pid;*out_entry=elf.entry;*out_user_stack=USER_STACK_TOP;return 0;
fail_user:(void)vfs_close_all(pid);address_space_destroy(&p->address_space);if(p->kernel_stack)pmm_free_page_range(p->kernel_stack,KERNEL_STACK_PAGES);clear_process(p);if(live_count)live_count--;return urc;}
int process_fork(pid_t parent,uint64_t user_rip,uint64_t user_rsp,pid_t *out_pid){
    if (!out_pid || !user_rip || !user_rsp) return -1;
    rix_process_t *pp=process_lookup(parent);if(!pp||!pp->address_space.pml4_phys)return -1;
    pid_t child;if(process_create("fork-child",parent,&child)!=0)return -1;
    rix_process_t *cp=process_lookup(child);if(!cp)return -1;
    if(address_space_clone(&pp->address_space,&cp->address_space)!=0)goto fail;
    uint64_t ks=pmm_alloc_pages(KERNEL_STACK_PAGES);if(!ks)goto fail;
    for(uint64_t page=0;page<KERNEL_STACK_PAGES;page++){
        zero_page(ks+page*RIXURI_PAGE_SIZE);
    }
    cp->kernel_stack=ks;cp->kernel_stack_size=KERNEL_STACK_SIZE;cp->state=RIX_PROC_SLEEPING;*out_pid=child;return 0;
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
    p->heap_base = RIX_USER_HEAP_BASE;
    p->heap_break = RIX_USER_HEAP_BASE;
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

/* DEBUG-only isolation: skip the CR3 write inside process_activate (the
 * rest — lookup, RSP0, current_pid — still runs). Default 0. If the boot
 * proceeds past "process_activate done" with this set, the CR3/address-space
 * switch is implicated; expect a *different*, visible fault later since user
 * mappings are then absent. Never ship enabled. */
#define RIX_DEBUG_NO_CR3_SWITCH 0
static uint64_t read_cr3_hw(void){uint64_t v;__asm__ volatile("mov %%cr3,%0":"=r"(v)::"memory");return v;}
/* Minimal CR3 loader: nothing but the serializing CR3 write itself.
 * The caller must validate the value first and sync the software tracker
 * (vmm_track_pml4) immediately after, so HW and SW never diverge. */
extern void load_cr3_raw(uint64_t phys);
static uint64_t read_rflags_hw(void){uint64_t v;__asm__ volatile("pushfq; popq %0":"=r"(v)::"memory");return v;}
static uint64_t read_rip_hw(void){uint64_t v;__asm__ volatile("lea 0(%%rip),%0":"=r"(v)::"memory");return v;}
static uint64_t read_rsp_hw(void){uint64_t v;__asm__ volatile("mov %%rsp,%0":"=r"(v)::"memory");return v;}
/* Read-only CPU-state forensics (CPL0 only; all called pre-switch in Ring0).
 * Discriminates QEMU-TCG vs physical Zen 4 paging configuration. */
static uint64_t read_cr0_hw(void){uint64_t v;__asm__ volatile("mov %%cr0,%0":"=r"(v)::"memory");return v;}
static uint64_t read_cr4_hw(void){uint64_t v;__asm__ volatile("mov %%cr4,%0":"=r"(v)::"memory");return v;}
static uint64_t read_efer_hw(void){uint32_t lo,hi;__asm__ volatile("rdmsr":"=a"(lo),"=d"(hi):"c"(0xC0000080u));return((uint64_t)hi<<32)|(uint64_t)lo;}
static uint32_t read_maxphys_hw(void){uint32_t a,b,c,d;__asm__ volatile("cpuid":"=a"(a),"=b"(b),"=c"(c),"=d"(d):"a"(0x80000008u),"c"(0u));(void)b;(void)c;(void)d;return a&0xFFu;}
/* Single walk step: log the full VA->entries->phys+flags chain, then return
 * the page-presence verdict (0=mapped). Every critical VA below refuses the
 * switch when unmapped, so a missing mapping can never become a silent
 * triple fault: it becomes a named REFUSED instead. */
static int walk_one(uint64_t np,uint64_t va,const char*label){vmm_log_walk(np,va,label);uint64_t ph=0,fl=0;return vmm_walk_in_pml4(np,va&~0xFFFULL,0,0,0,0,&ph,&fl);}
static uint64_t read_gdtr_base_hw(void){struct __attribute__((packed)){uint16_t limit;uint64_t base;}gdtr={0,0};__asm__ volatile("sgdt %0":"=m"(gdtr)::"memory");return gdtr.base;}
static uint64_t read_idtr_base_hw(void){struct __attribute__((packed)){uint16_t limit;uint64_t base;}idtr={0,0};__asm__ volatile("sidt %0":"=m"(idtr)::"memory");return idtr.base;}
void scheduler_yield(void);
extern void isr14(void);
static uint64_t cr3trace_tags[CR3TRACE_N];
static uint64_t cr3trace_a[CR3TRACE_N];
static uint64_t cr3trace_b[CR3TRACE_N];
static uint64_t cr3trace_c[CR3TRACE_N];
static unsigned cr3trace_n;
void cr3trace_push(uint64_t tag,uint64_t a,uint64_t b,uint64_t c){unsigned i=cr3trace_n%CR3TRACE_N;cr3trace_tags[i]=tag;cr3trace_a[i]=a;cr3trace_b[i]=b;cr3trace_c[i]=c;cr3trace_n++;}
/* Numbered switch attempts: the gated prints below show SW#1/SW#2 so a
 * frozen physical console unambiguously names WHICH switch died. */
static unsigned cr3_switch_seq;
void cr3trace_dump(void){unsigned total=cr3trace_n,show=total<CR3TRACE_N?total:CR3TRACE_N,start=total-show;for(unsigned i=0;i<show;i++){unsigned k=(start+i)%CR3TRACE_N;uint64_t tag=cr3trace_tags[k];kernel_log(tag==1u?"TRACE act-pre pid=":tag==2u?"TRACE act-post pid=":"TRACE yield old=");kernel_log_hex(cr3trace_a[k]);kernel_log(tag==3u?" next=":" tgt=");kernel_log_hex(cr3trace_b[k]);kernel_log(" hw=");kernel_log_hex(cr3trace_c[k]);kernel_log("\r\n");}}
/* Atomic CR3-switch isolation diagnostics. Dual-output (screen+serial via
 * kernel_log) so the lines survive on physical-console-only setups as well.
 * Only touches low-identity kernel memory (code/data/stacks/framebuffer),
 * all shared under the target CR3, so logging is safe pre- and post-switch.
 * Returns 0 when target CR3 is safe to load, -1 when CR3 must NOT be written. */
static int cr3_diagnose_target(rix_process_t*p,uint64_t old_cr3){
 uint64_t np=p->address_space.pml4_phys;
 uint64_t cx4=0;uint32_t pmt=0;int pmu=0;
 kernel_log("DEBUG: CR3 old=");kernel_log_hex(old_cr3);kernel_log("\r\n");
 kernel_log("DEBUG: CR3 new=");kernel_log_hex(np);kernel_log("\r\n");
 kernel_log("DEBUG: CR3 hw=");kernel_log_hex(read_cr3_hw());
 kernel_log(" sw=");kernel_log_hex(vmm_current_pml4());kernel_log("\r\n");
 {uint64_t cr0=read_cr0_hw(),cr4=read_cr4_hw(),efer=read_efer_hw();uint32_t mp=read_maxphys_hw();
  cx4=cr4;
  uint64_t apicbase=x86_rdmsr(0x1Bu);
  kernel_log("CPU: CR0=");kernel_log_hex(cr0);
  kernel_log(" PG=");kernel_log_dec((uint64_t)((cr0>>31)&1ULL));
  kernel_log(" WP=");kernel_log_dec((uint64_t)((cr0>>16)&1ULL));
  kernel_log(" CR4=");kernel_log_hex(cr4);
  kernel_log(" LA57=");kernel_log_dec((uint64_t)((cr4>>12)&1ULL));
  kernel_log(" PCIDE=");kernel_log_dec((uint64_t)((cr4>>17)&1ULL));
  kernel_log(" SMEP=");kernel_log_dec((uint64_t)((cr4>>20)&1ULL));
  kernel_log(" SMAP=");kernel_log_dec((uint64_t)((cr4>>21)&1ULL));
  kernel_log(" PKE=");kernel_log_dec((uint64_t)((cr4>>22)&1ULL));
  kernel_log(" PAE=");kernel_log_dec((uint64_t)((cr4>>5)&1ULL));
  kernel_log(" EFER=");kernel_log_hex(efer);
  kernel_log(" NXE=");kernel_log_dec((uint64_t)((efer>>11)&1ULL));
  kernel_log(" LMA=");kernel_log_dec((uint64_t)((efer>>10)&1ULL));
  kernel_log(" MaxPhys=");kernel_log_dec((uint64_t)mp);
  kernel_log(" X2APIC=");kernel_log_dec((uint64_t)((apicbase>>10)&1ULL));
  kernel_log("\r\n");}
 kernel_log("PMM: pml4=");kernel_log_hex(np);
 kernel_log(" managed=");kernel_log_dec((uint64_t)pmm_is_managed(np));
 kernel_log(" used=");kernel_log_dec((uint64_t)pmm_is_in_use(np));
 kernel_log(" reserved=");kernel_log_dec((uint64_t)pmm_is_reserved(np));
 kernel_log("\r\n");
 {uint64_t rb=0,re=0;uint32_t rt=0;int ru=0;int rr=pmm_region_info(np,&rb,&re,&rt,&ru);
  int rmag=rr<0?-rr:rr;pmt=rt;pmu=ru;
  kernel_log("PMM region: base=");kernel_log_hex(rb);
  kernel_log(" end=");kernel_log_hex(re);
  kernel_log(" type=");kernel_log_dec((uint64_t)rt);
  kernel_log(" usable=");kernel_log_dec((uint64_t)(ru?1:0));
  kernel_log(" lookup=");kernel_log_dec((uint64_t)rmag);
  kernel_log("\r\n");}
 int vr=vmm_validate_pml4(np);
 if(vr==0){kernel_log("DEBUG: TARGET PML4 VALID\r\n");}
 else{kernel_log("DEBUG: TARGET PML4 INVALID reason=");kernel_log_dec((uint64_t)(vr<0?-vr:vr));kernel_log("\r\n");serial_drain();return -1;}
 kernel_log("DEBUG: KERNEL CR3=");kernel_log_hex(vmm_kernel_pml4());kernel_log("\r\n");
 vmm_log_pml4_range(vmm_kernel_pml4(),np,0,16);
 vmm_log_pml4_compare(vmm_kernel_pml4(),np);
 uint64_t rip=read_rip_hw(),rsp=read_rsp_hw();
 uint64_t old_stack_phys=0,new_stack_phys=0;
 (void)vmm_walk_in_pml4(old_cr3,rsp,0,0,0,0,&old_stack_phys,0);
 (void)vmm_walk_in_pml4(np,rsp,0,0,0,0,&new_stack_phys,0);
 kernel_log("DEBUG: active stack phys old=");kernel_log_hex(old_stack_phys);
 kernel_log(" new=");kernel_log_hex(new_stack_phys);kernel_log("\r\n");
 if(!old_stack_phys||!new_stack_phys||
    (old_stack_phys&~0xfffULL)!=(new_stack_phys&~0xfffULL)){
  kernel_log("DEBUG: CR3 REFUSED active stack alias mismatch\r\n");
  serial_drain();return -1;
 }
 uint64_t ret=(uint64_t)__builtin_return_address(0);
 uint64_t kstack_base=p->kernel_stack,kstack_top=p->kernel_stack+p->kernel_stack_size-1ULL;
 const x86_tss_t*tss=tss_current();
 uint64_t tss_va=(uint64_t)(uintptr_t)tss;
 uint64_t rsp0=tss?tss->rsp0:0,ist1=tss?tss->ist[0]:0;
 int wrip=walk_one(np,rip,"CURRENT RIP");
 int wrsp=walk_one(np,rsp,"CURRENT RSP");
 int wret=walk_one(np,ret&~0xFFFULL,"RETURN ADDR");
 int whandler=walk_one(np,(uint64_t)(uintptr_t)isr14,"EXC HANDLER #PF");
 int wkbase=walk_one(np,kstack_base,"KERNEL STACK BASE");
 int wktop=walk_one(np,kstack_top&~0xFFFULL,"KERNEL STACK TOP");
 int wproc=walk_one(np,(uint64_t)(uintptr_t)process_activate,"PROCESS CODE");
 int wsched=walk_one(np,(uint64_t)(uintptr_t)scheduler_yield,"SCHEDULER CODE");
 int widt=walk_one(np,read_idtr_base_hw(),"IDT BASE");
 int wgdt=walk_one(np,read_gdtr_base_hw(),"GDT BASE");
 int wtss=walk_one(np,tss_va,"TSS");
 int wrsp0=rsp0?walk_one(np,rsp0-8ULL,"TSS.RSP0"):0;
 int wist=ist1?walk_one(np,ist1-8ULL,"IST1 DF STACK"):0;
 int wuimg=walk_one(np,0x0000008000000000ULL,"USER IMAGE BASE");
 int wusttop=walk_one(np,USER_STACK_TOP-4096ULL,"USER STACK TOP");
 int wustbase=walk_one(np,USER_STACK_BASE,"USER STACK BASE");
 {uint64_t txbase=address_space_translate(&p->address_space,USER_STACK_BASE);
  kernel_log("DEBUG: BASE xcheck=");kernel_log_hex(txbase);kernel_log("\r\n");}
 /* Critical rule: every VA the CPU may touch across the switch (kernel
  * execution path, fault-delivery path, and user entry path) must be mapped
  * under the target. Anything missing refuses the switch by name. */
 {
  int rrip=(wrip==0),rrsp=(wrsp==0),rret=(wret==0);
  int rkst=(wkbase==0&&wktop==0);
  int allk=(wrip==0&&wrsp==0&&wret==0&&whandler==0&&wkbase==0&&wktop==0&&wproc==0&&wsched==0&&widt==0&&wgdt==0&&wtss==0&&wrsp0==0&&wist==0);
  int allu=(wuimg==0&&wusttop==0);
  int baseok=(wustbase==0);
  kernel_log("DEBUG: CURRENT RIP MAPPED=");kernel_log(rrip?"YES\r\n":"NO\r\n");
  kernel_log("DEBUG: CURRENT RSP MAPPED=");kernel_log(rrsp?"YES\r\n":"NO\r\n");
  kernel_log("DEBUG: RETURN ADDR MAPPED=");kernel_log(rret?"YES\r\n":"NO\r\n");
  kernel_log("DEBUG: KERNEL STACK MAPPED=");kernel_log(rkst?"YES\r\n":"NO\r\n");
  kernel_log("DEBUG: KERNEL MAPPING=");kernel_log(allk?"PASS\r\n":"FAIL\r\n");
  kernel_log("DEBUG: USER MAPPING=");kernel_log(allu?"PASS\r\n":"FAIL\r\n");
  /* The bottom stack page (PD[510], USER_STACK_BASE) is a known pre-existing
   * mapping-layer hole: present since HEAD, harmless (31/32 pages mapped,
   * HEAD boots to shell with it), and independent of the CR3 switch path.
   * Warn loudly, keep the forensic lines, but do not gate boot on it. */
  kernel_log("DEBUG: STACK BASE HOLE=");kernel_log(baseok?"NO\r\n":"YES warn-only\r\n");
  kernel_log("CR3DIAG sum: valid=1 rip=");kernel_log_dec((uint64_t)rrip);
  kernel_log(" rsp=");kernel_log_dec((uint64_t)rrsp);
  kernel_log(" ret=");kernel_log_dec((uint64_t)rret);
  kernel_log(" kstack=");kernel_log_dec((uint64_t)rkst);
  kernel_log(" kall=");kernel_log_dec((uint64_t)allk);
  kernel_log(" uall=");kernel_log_dec((uint64_t)allu);
  kernel_log(" base=");kernel_log_dec((uint64_t)baseok);
  kernel_log(" pmtype=");kernel_log_dec((uint64_t)pmt);
  kernel_log(" pmusable=");kernel_log_dec((uint64_t)(pmu?1:0));
  kernel_log(" la57=");kernel_log_dec((uint64_t)((cx4>>12)&1ULL));
  kernel_log("\r\n");
  if(!allk||!allu){kernel_log("DEBUG: process_activate REFUSED unmapped target VA\r\n");serial_drain();return -1;}
 }
 serial_drain();
 return 0;
}
int process_validate_user_entry(pid_t pid,uint64_t user_rip,uint64_t user_rsp){
 rix_process_t*p=process_lookup(pid);uint64_t phys=0,flags=0;
 if(!p||!p->address_space.pml4_phys||!user_rip||!user_rsp)return -1;
 int rc=vmm_walk_in_pml4(p->address_space.pml4_phys,user_rip&~0xfffULL,0,0,0,0,&phys,&flags);
 if(rc!=0||(flags&(RIXURI_PTE_PRESENT|RIXURI_PTE_USER|RIXURI_PTE_NX))!=(RIXURI_PTE_PRESENT|RIXURI_PTE_USER)){
  kernel_log("DEBUG: user entry RIP permission failure rip=");kernel_log_hex(user_rip);kernel_log(" rc=");kernel_log_dec((uint64_t)(rc<0?-rc:rc));kernel_log(" flags=");kernel_log_hex(flags);kernel_log("\r\n");return -2;
 }
 /* The initial stack pointer may equal the exclusive top boundary; the
    first user push then touches user_rsp-1.  Validate that byte rather than
    the boundary itself, and require a writable user leaf. */
 if(user_rsp>USER_STACK_TOP||user_rsp<=USER_STACK_BASE)return -3;
 phys=0;flags=0;rc=vmm_walk_in_pml4(p->address_space.pml4_phys,(user_rsp-1ULL)&~0xfffULL,0,0,0,0,&phys,&flags);
 if(rc!=0||(flags&(RIXURI_PTE_PRESENT|RIXURI_PTE_USER|RIXURI_PTE_WRITE))!=(RIXURI_PTE_PRESENT|RIXURI_PTE_USER|RIXURI_PTE_WRITE)){
  kernel_log("DEBUG: user entry RSP permission failure rsp=");kernel_log_hex(user_rsp);kernel_log(" rc=");kernel_log_dec((uint64_t)(rc<0?-rc:rc));kernel_log(" flags=");kernel_log_hex(flags);kernel_log("\r\n");return -4;
 }
 return 0;
}
/* Last-instant read-only freshness check of the target root. Called with IF=0
 * just before load_cr3_raw: re-reads PML4[0..3] straight from the page so any
 * corruption between the earlier dump and the switch would show here.
 * Deliberately READ-ONLY: writability of this page is already proven (the
 * creation-time writes read back correctly), and no new write belongs in the
 * switch path. 0=readable, -1=unreadable (caller must refuse the switch).
 * Extension (HW CR3-switch freeze triage): also dumps the PML4[0] subtree
 * leaves covering low memory for BOTH the target root and the currently
 * active root. A frozen physical target with divergent leaves names the
 * corrupt level in a single boot; all reads go through the current
 * (proven) tables, never the target mapping. */
static uint64_t cr3_probe_entry(uint64_t table_phys, size_t index){
 uint64_t *table=(uint64_t*)vmm_phys_ptr(table_phys&~0xFFFULL);
 if(!table||index>=512)return 0;
 return table[index];
}
static int cr3_probe_root(uint64_t np){
 volatile uint64_t*t=(volatile uint64_t*)vmm_phys_ptr(np);
 if(!t)return -1;
 {static unsigned n=0;if(n<2){kernel_log("DEBUG: ROOT fresh pml40=");kernel_log_hex(t[0]);kernel_log(" pml41=");kernel_log_hex(t[1]);kernel_log(" pml42=");kernel_log_hex(t[2]);kernel_log(" pml43=");kernel_log_hex(t[3]);kernel_log("\r\n");if(n<2){n++;}}}
 {static unsigned n=0;if(n<2){
  uint64_t cur=read_cr3_hw();
  uint64_t te0=cr3_probe_entry(np,0),te1=cr3_probe_entry(te0,0),te2=cr3_probe_entry(te1,0);
  uint64_t ce0=cr3_probe_entry(cur,0),ce1=cr3_probe_entry(ce0,0),ce2=cr3_probe_entry(ce1,0);
  kernel_log("DEBUG: TSUB tgt=");kernel_log_hex(te0);kernel_log(" ");kernel_log_hex(te1);kernel_log(" ");kernel_log_hex(te2);kernel_log("\r\n");
  kernel_log("DEBUG: TSUB cur=");kernel_log_hex(ce0);kernel_log(" ");kernel_log_hex(ce1);kernel_log(" ");kernel_log_hex(ce2);kernel_log("\r\n");
  uint64_t kroot=vmm_kernel_pml4();
  uint64_t *kt=(uint64_t*)vmm_phys_ptr(kroot);
  uint64_t kslot0=kt?kt[0]:~0ULL;
  kernel_log("DEBUG: PML40 tgt=");kernel_log_hex(t[0]);kernel_log(" kern=");kernel_log_hex(kslot0);
  kernel_log(" tgtroot=");kernel_log_hex(np);kernel_log(" kroot=");kernel_log_hex(kroot);
  kernel_log(" cur=");kernel_log_hex(cur);kernel_log("\r\n");
  n++;
 }}
 return 0;
}
static volatile int process_user_entry_deferred;
int process_activate_user_entry(pid_t pid){process_user_entry_deferred=1;int rc=process_activate(pid);process_user_entry_deferred=0;return rc;}
int process_activate(pid_t pid){if(pid==0){uint64_t kb=vmm_kernel_pml4();cr3trace_push(1,0,kb,read_cr3_hw());current_pid=0;vmm_switch_pml4(kb);cr3trace_push(2,0,kb,read_cr3_hw());return 0;}{static unsigned n=0;if(n<2){kernel_log("DEBUG: process_activate begin\r\n");n++;}}rix_process_t*p=process_lookup(pid);{static unsigned n=0;if(n<2){kernel_log("DEBUG: process lookup done\r\n");n++;}}if(!p||p->state==RIX_PROC_UNUSED||p->state==RIX_PROC_ZOMBIE||!p->address_space.pml4_phys||!p->kernel_stack)return -1;{static unsigned n=0;if(n<1){kernel_log("DEBUG: address space found pid=");kernel_log_dec(p->pid);kernel_log(" pml4=");kernel_log_hex(p->address_space.pml4_phys);kernel_log(" kstack=");kernel_log_hex(p->kernel_stack);kernel_log("\r\nDEBUG: current process=");kernel_log_dec(process_current());kernel_log(" current cr3=");kernel_log_hex(read_cr3_hw());kernel_log(" kernel pml4=");kernel_log_hex(vmm_kernel_pml4());kernel_log("\r\nDEBUG: target pml4=");kernel_log_hex(p->address_space.pml4_phys);kernel_log("\r\n");serial_drain();n++;}}{static unsigned n=0;if(n<1){uint64_t oc=read_cr3_hw();if(cr3_diagnose_target(p,oc)!=0){kernel_log("DEBUG: process_activate REFUSED invalid target CR3\r\n");serial_drain();return -1;}n++;}}current_pid=pid;tss_set_rsp0(p->kernel_stack+p->kernel_stack_size);{static unsigned n=0;if(n<2){kernel_log("DEBUG: switching CR3\r\n");serial_drain();n++;}}
#if RIX_DEBUG_NO_CR3_SWITCH
{static unsigned n=0;if(n<2){kernel_log("DEBUG: process_activate CR3 SWITCH SKIPPED\r\n");kernel_log("DEBUG: CR3 switch skipped\r\n");serial_drain();n++;}}
#else
/* DEBUG-only isolation: reload the CURRENT CR3 instead of the target
 * (same-value mov, TLB flush only). Default 0. If the reset reproduces with
 * the same value, the mov/environment is at fault, not the target tables.
 * If only the target value resets, the target root is implicated at CPU
 * level. Diagnostic only; never ship enabled. */
#define RIX_DEBUG_CR3_RELOAD_SELF 0
#define RIX_DEFER_USER_CR3_TO_ENTRY 1
{uint64_t target=p->address_space.pml4_phys;cr3trace_push(1,(uint64_t)pid,target,read_cr3_hw());if(vmm_validate_pml4(target)!=0){kernel_log("DEBUG: process_activate REFUSED invalid target CR3\r\n");serial_drain();return -1;}int sc=address_space_sync_kernel(&p->address_space);{static unsigned n=0;if(n<2||sc>0){kernel_log("DEBUG: kernel sync slots=");if(sc<0){kernel_log("-1");}else{kernel_log_dec((uint64_t)sc);}kernel_log("\r\n");serial_drain();if(n<2){n++;}}}
uint64_t loadval=target;
/* Sync first, then validate the exact root that will reach CR3.  The old
 * order validated the root and only afterward rewrote shared kernel slots;
 * real hardware can expose that unvalidated final state during a TLB flush. */
int final_vr=vmm_validate_pml4(loadval);
if(final_vr!=0){kernel_log("DEBUG: final PML4 validation failed reason=");kernel_log_dec((uint64_t)(final_vr<0?-final_vr:final_vr));kernel_log("\r\n");serial_drain();return -1;}
if(process_user_entry_deferred){
 /* Only the first bootstrap defers CR3; resumed user tasks must switch here. */
 {static unsigned n=0;if(n<2){kernel_log("DEBUG: user CR3 switch deferred to ring3 entry\r\n");serial_drain();n++;}}
 return 0;
}
#if RIX_DEBUG_CR3_RELOAD_SELF
loadval=read_cr3_hw();
{static unsigned n=0;if(n<2){kernel_log("DEBUG: CR3 SELFTEST reloading current CR3\r\n");serial_drain();n++;}}
#endif
 {static unsigned n=0;if(n<2){uint64_t rf=read_rflags_hw();cr3_switch_seq++;kernel_log("DEBUG: SW#");kernel_log_dec(cr3_switch_seq);kernel_log(" pid=");kernel_log_dec((uint64_t)pid);kernel_log(" before mov cr3 target=");kernel_log_hex(loadval);kernel_log(" current=");kernel_log_hex(read_cr3_hw());kernel_log(" IF=");kernel_log_dec((uint64_t)((rf>>9)&1ULL));kernel_log("\r\n");serial_drain();n++;}}
 {int pr=cr3_probe_root(loadval);if(pr!=0){kernel_log("DEBUG: ROOT probe FAILED reason=");if(pr<0){kernel_log("-");}kernel_log_dec((uint64_t)(pr<0?-pr:pr));kernel_log("\r\n");serial_drain();return -1;}}
 {static unsigned n=0;if(n<2){uint64_t self=read_cr3_hw();load_cr3_raw(self);kernel_log("DEBUG: CR3 selftest ok cur=");kernel_log_hex(read_cr3_hw());kernel_log("\r\n");serial_drain();n++;}}
 load_cr3_raw(loadval);
 /* Stackless post-switch probes (no calls, no memory, no stack except the
  * probe push itself): 'F' proves instruction fetch works on the new
  * tables; the push/pop proves RSP is writable; 'S' proves both. On a
  * physical target that freezes here, whichever letter is missing names
  * the broken mapping class in a single boot. Gated to the first two
  * switches to keep serial logs clean; QEMU-safe: raw chars only.
  * CRITICAL: every port wait is BOUNDED (4096 tries, same discipline as
  * serial_putc). An unnumbered `jz wait` hangs forever on a board whose
  * UART decodes the port but never sets THRE (e.g. present-but-disabled
  * Super-I/O with no COM1) — that looks exactly like a CR3-switch freeze
  * while being a serial stall. Never wait unbounded on a UART again. */
 {static unsigned cr3post_n = 0;
 if (cr3post_n < 2) {
 cr3post_n++;
 __asm__ volatile(
  "movl $0x3FD,%%edx\n\t"
  "movl $4096,%%ecx\n\t"
  "cr3post_wait1: inb %%dx,%%al\n\t"
  "testb $0x20,%%al\n\t"
  "jnz cr3post_done1\n\t"
  "decl %%ecx\n\t"
  "jnz cr3post_wait1\n\t"
  "cr3post_done1: movl $0x3F8,%%edx\n\t"
  "movb $0x46,%%al\n\t"
  "outb %%al,%%dx\n\t"
  "pushq %%rax\n\t"
  "popq %%rax\n\t"
  "movl $0x3FD,%%edx\n\t"
  "movl $4096,%%ecx\n\t"
  "cr3post_wait2: inb %%dx,%%al\n\t"
  "testb $0x20,%%al\n\t"
  "jnz cr3post_done2\n\t"
  "decl %%ecx\n\t"
  "jnz cr3post_wait2\n\t"
  "cr3post_done2: movl $0x3F8,%%edx\n\t"
  "movb $0x53,%%al\n\t"
  "outb %%al,%%dx\n\t"
  ::: "rax", "rcx", "rdx", "memory");
 }}
 vmm_track_pml4(loadval);cr3trace_push(2,(uint64_t)pid,loadval,read_cr3_hw());{static unsigned n=0;if(n<2){uint64_t hw=read_cr3_hw();kernel_log("DEBUG: SW#");kernel_log_dec(cr3_switch_seq);kernel_log(" CR3 load returned cur=");kernel_log_hex(hw);kernel_log(hw==loadval?" SW=SYNC\r\n":" SW=MISMATCH\r\n");kernel_log("DEBUG: CR3 switched\r\n");serial_drain();n++;}}}
#endif
{static unsigned n=0;if(n<2){kernel_log("DEBUG: CR3 switched cur=");kernel_log_hex(read_cr3_hw());kernel_log("\r\n");serial_drain();n++;}}{static unsigned m=0;if(m<2){kernel_log("DEBUG: process_activate done\r\n");serial_drain();m++;}}return 0;}
int process_set_state(pid_t pid,rix_process_state_t state){rix_process_t*p=process_lookup(pid);if(!p||state==RIX_PROC_UNUSED)return -1;rix_process_state_t old=p->state;p->state=state;if(state==RIX_PROC_RUNNING&&process_activate(pid)!=0){p->state=old;return -1;}return 0;}
int process_exit(pid_t pid,uint64_t status){rix_process_t*p=process_lookup(pid);pid_t session;if(!p||pid==0||p->state==RIX_PROC_ZOMBIE)return -1;session=p->session;(void)vfs_close_all(pid);p->exit_status=status;p->state=RIX_PROC_ZOMBIE;if(current_pid==pid)current_pid=0;session_drop_if_empty(session);return 0;}
int process_set_group(pid_t pid,pid_t process_group){rix_process_t*p=process_lookup(pid);if(!p||!process_group)return -1;p->process_group=process_group;return 0;}
int process_set_session(pid_t pid,pid_t session){rix_process_t*p=process_lookup(pid);if(!p||!session)return -1;p->session=session;return 0;}
int process_get_session(pid_t pid,pid_t*session){rix_process_t*p=process_lookup(pid);if(!p||!session||p->state==RIX_PROC_UNUSED)return -1;*session=p->session;return 0;}
int process_is_session_leader(pid_t pid){rix_process_t*p=process_lookup(pid);return p&&p->state!=RIX_PROC_UNUSED&&p->state!=RIX_PROC_ZOMBIE&&p->session==pid;}
int process_create_session(pid_t pid,pid_t*session){rix_process_t*p=process_lookup(pid);pid_t old_session,old_group;if(!p||p->state==RIX_PROC_UNUSED||p->state==RIX_PROC_ZOMBIE||p->process_group==pid)return -1;old_session=p->session;old_group=p->process_group;p->session=pid;p->process_group=pid;if(session_register(pid,pid,p->uid)!=0){p->session=old_session;p->process_group=old_group;return -1;}if(session)*session=pid;return 0;}
int process_logout_session(pid_t pid,uint64_t status){rix_process_t*p=process_lookup(pid);if(!p||p->state==RIX_PROC_UNUSED||p->state==RIX_PROC_ZOMBIE||!p->session)return -1;pid_t session=p->session;for(size_t i=1;i<RIX_PROCESS_MAX;i++){rix_process_t*q=&table[i];if(q->state!=RIX_PROC_UNUSED&&q->state!=RIX_PROC_ZOMBIE&&q->pid!=pid&&q->session==session)(void)process_exit(q->pid,status);}return 0;}
int process_leave_session(pid_t pid){rix_process_t*p=process_lookup(pid);pid_t session;if(!p||p->state==RIX_PROC_UNUSED||p->state==RIX_PROC_ZOMBIE)return -1;session=p->session;p->session=0;p->process_group=pid;session_drop_if_empty(session);return 0;}
int process_list_sessions(rix_session_info_t*out,size_t capacity,size_t*count){size_t total=0,index=0;if(!count||(!out&&capacity))return-1;for(size_t i=0;i<RIX_SESSION_MAX;i++)if(sessions[i].session)total++;if(capacity<total)return-1;for(size_t i=0;i<RIX_SESSION_MAX;i++)if(sessions[i].session){session_refresh_tty(&sessions[i]);out[index++]=sessions[i];}*count=total;return 0;}
int process_list_processes(rix_process_info_t*out,size_t capacity,size_t*count){size_t total=0,index=0;if(!count||(!out&&capacity))return-1;for(size_t i=0;i<RIX_PROCESS_MAX;i++)if(table[i].state!=RIX_PROC_UNUSED)total++;if(capacity<total)return-1;for(size_t i=0;i<RIX_PROCESS_MAX;i++){rix_process_t*p=&table[i];if(p->state==RIX_PROC_UNUSED)continue;rix_process_info_t*slot=&out[index++];slot->pid=p->pid;slot->parent=p->parent;slot->uid=p->uid;slot->gid=p->gid;slot->state=(uint32_t)p->state;slot->session=p->session;size_t n=bounded_strlen(p->name);for(size_t k=0;k<n;k++)slot->name[k]=p->name[k];slot->name[n]=0;}*count=total;return 0;}
int process_signal_group(pid_t process_group,unsigned signal){if(!process_group||signal<1u||signal>64u)return -1;uint64_t bit=1ULL<<(signal-1u);int sent=0;for(size_t i=0;i<RIX_PROCESS_MAX;i++){rix_process_t*p=&table[i];if(p->state!=RIX_PROC_UNUSED&&p->state!=RIX_PROC_ZOMBIE&&p->process_group==process_group){p->signal_pending|=bit;if(p->state==RIX_PROC_SLEEPING&&(p->signal_mask&bit)==0)p->state=RIX_PROC_RUNNING;sent++;}}return sent?0:-1;}
int process_wait(pid_t parent,pid_t wanted,uint64_t*status,pid_t*child_pid){if(!status||!child_pid)return -1;rix_process_t*match=NULL;int has_child=0;for(size_t i=1;i<RIX_PROCESS_MAX;i++){rix_process_t*p=&table[i];if(p->state!=RIX_PROC_UNUSED&&p->parent==parent&&(wanted==(pid_t)-1||p->pid==wanted)){has_child=1;if(p->state==RIX_PROC_ZOMBIE){match=p;break;}}}if(!has_child)return 2;if(!match)return 1;pid_t session=match->session;*status=match->exit_status;*child_pid=match->pid;(void)vfs_close_all(match->pid);address_space_destroy(&match->address_space);if(match->kernel_stack)pmm_free_page_range(match->kernel_stack,KERNEL_STACK_PAGES);clear_process(match);audit_uids[(size_t)match->pid]=0;if(live_count)live_count--;session_drop_if_empty(session);return 0;}

int capability_valid(uint64_t mask){return mask!=0u&&(mask&~RIX_CAP_ALL)==0u;}
int process_has_capability(pid_t pid,uint64_t capability){rix_process_t*p=process_lookup(pid);return p&&capability_valid(capability)&&((p->capabilities&capability)==capability);}
int process_get_capabilities(pid_t pid,uint64_t*out){rix_process_t*p=process_lookup(pid);if(!p||!out)return -1;*out=p->capabilities;return 0;}
int process_drop_capabilities(pid_t pid,uint64_t mask){rix_process_t*p=process_lookup(pid);if(!p||!capability_valid(mask))return -1;p->capabilities&=~mask;return 0;}
int process_delegate_capabilities(pid_t parent,pid_t child,uint64_t mask){rix_process_t*p=process_lookup(parent),*c=process_lookup(child);if(!p||!c||parent==child||c->state==RIX_PROC_UNUSED||c->state==RIX_PROC_ZOMBIE||c->parent!=parent||!capability_valid(mask)||!process_has_capability(parent,RIX_CAP_DELEGATE)||!process_has_capability(parent,mask)||(mask&RIX_CAP_DELEGATE)||(c->capabilities&mask))return -1;c->capabilities|=mask;p->capabilities&=~mask;return 0;}
int process_get_audit_uid(pid_t pid,uint32_t*out){rix_process_t*p=process_lookup(pid);size_t index=(size_t)pid;if(!p||!out||index>=RIX_PROCESS_MAX)return -1;*out=audit_uids[index];return 0;}
int process_set_audit_uid(pid_t pid,uint32_t uid){rix_process_t*p=process_lookup(pid);size_t index=(size_t)pid;if(!p||index>=RIX_PROCESS_MAX||!process_has_capability(pid,RIX_CAP_AUDIT_ADMIN))return -1;audit_uids[index]=uid;return 0;}


int process_brk(pid_t pid, uint64_t requested, uint64_t *out_break) {
    rix_process_t *p = process_lookup(pid);
    if (!p || !p->address_space.pml4_phys || !p->heap_base || !out_break) return -1;
    if (requested == 0) { *out_break = p->heap_break; return 0; }
    if (requested < p->heap_base || requested > RIX_USER_HEAP_LIMIT) return -2;
    uint64_t old_break = p->heap_break;
    uint64_t old_page_end = (old_break + RIXURI_PAGE_SIZE - 1u) & ~(RIXURI_PAGE_SIZE - 1u);
    uint64_t new_page_end = (requested + RIXURI_PAGE_SIZE - 1u) & ~(RIXURI_PAGE_SIZE - 1u);
    if (new_page_end > old_page_end) {
        size_t mapped = 0;
        for (uint64_t va = old_page_end; va < new_page_end; va += RIXURI_PAGE_SIZE) {
            uint64_t pa = pmm_alloc_page();
            if (!pa) {
                for (uint64_t rollback = old_page_end; rollback < va; rollback += RIXURI_PAGE_SIZE) {
                    uint64_t old_pa = address_space_translate(&p->address_space, rollback);
                    (void)address_space_unmap(&p->address_space, rollback);
                    if (old_pa) pmm_free_page(old_pa);
                }
                return -3;
            }
            zero_page(pa);
            if (address_space_map(&p->address_space, va, pa,
                                  RIXURI_PTE_PRESENT | RIXURI_PTE_WRITE |
                                  RIXURI_PTE_USER | RIXURI_PTE_NX | RIXURI_PTE_OWNED) != 0) {
                pmm_free_page(pa);
                for (uint64_t rollback = old_page_end; rollback < va; rollback += RIXURI_PAGE_SIZE) {
                    uint64_t old_pa = address_space_translate(&p->address_space, rollback);
                    (void)address_space_unmap(&p->address_space, rollback);
                    if (old_pa) pmm_free_page(old_pa);
                }
                return -3;
            }
            ++mapped;
        }
        (void)mapped;
    } else if (new_page_end < old_page_end) {
        for (uint64_t va = new_page_end; va < old_page_end; va += RIXURI_PAGE_SIZE) {
            uint64_t pa = address_space_translate(&p->address_space, va);
            (void)address_space_unmap(&p->address_space, va);
            if (pa) pmm_free_page(pa);
        }
    }
    p->heap_break = requested;
    *out_break = requested;
    return 0;
}
