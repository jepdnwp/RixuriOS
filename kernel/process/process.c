#include "process.h"
#include "../elf/loader.h"
#include "../mm/pmm.h"
#include "../arch/x86_64/tss.h"
#include <stddef.h>

#define USER_STACK_TOP 0x00007FFFFFFFF000ULL
#define USER_STACK_PAGES 8ULL
#define USER_STACK_BASE (USER_STACK_TOP - USER_STACK_PAGES * 4096ULL)
#define KERNEL_STACK_SIZE 4096ULL

static rix_process_t table[RIX_PROCESS_MAX];
static pid_t current_pid;
static pid_t next_pid;
static size_t live_count;

static size_t bounded_strlen(const char *s){size_t n=0;if(!s)return 0;while(n<RIX_PROCESS_NAME_MAX-1&&s[n])n++;return n;}
static void copy_name(char *dst,const char *src){size_t n=bounded_strlen(src);for(size_t i=0;i<n;i++)dst[i]=src[i];dst[n]=0;}
static void clear_process(rix_process_t *p){
    p->pid=0;p->parent=0;p->state=RIX_PROC_UNUSED;p->uid=0;p->gid=0;p->address_space.pml4_phys=0;
    p->kernel_stack=0;p->kernel_stack_size=0;p->exit_status=0;p->fd_bitmap=0;p->name[0]=0;
}
static void zero_page(uint64_t pa){uint8_t *p=(uint8_t *)(uintptr_t)pa;for(size_t i=0;i<4096;i++)p[i]=0;}

int process_init(void){
    for(size_t i=0;i<RIX_PROCESS_MAX;i++)clear_process(&table[i]);
    current_pid=0;next_pid=1;live_count=0;
    table[0].pid=0;table[0].state=RIX_PROC_RUNNING;table[0].uid=0;table[0].gid=0;copy_name(table[0].name,"kernel");live_count=1;
    return 0;
}

pid_t process_current(void){return current_pid;}
rix_process_t *process_lookup(pid_t pid){for(size_t i=0;i<RIX_PROCESS_MAX;i++)if(table[i].state!=RIX_PROC_UNUSED&&table[i].pid==pid)return &table[i];return NULL;}
size_t process_count(void){return live_count;}

int process_create(const char *name,pid_t parent,pid_t *out_pid){
    if(!name||!name[0])return -1;
    if(parent!=0&&!process_lookup(parent))return -1;
    for(size_t i=1;i<RIX_PROCESS_MAX;i++)if(table[i].state==RIX_PROC_UNUSED){
        pid_t pid=next_pid++;if(!pid)pid=next_pid++;
        clear_process(&table[i]);table[i].pid=pid;table[i].parent=parent;table[i].state=RIX_PROC_SLEEPING;
        table[i].uid=0;table[i].gid=0;copy_name(table[i].name,name);live_count++;if(out_pid)*out_pid=pid;return 0;
    }
    return -1;
}

int process_create_user(const char *name,pid_t parent,const void *image,uint64_t image_size,
                        uint64_t *out_entry,uint64_t *out_user_stack){
    if(!name||!name[0]||!image||!image_size||!out_entry||!out_user_stack)return -1;
    if(parent!=0&&!process_lookup(parent))return -1;
    pid_t pid=0;if(process_create(name,parent,&pid)!=0)return -1;
    rix_process_t *p=process_lookup(pid);if(!p)return -1;
    if(address_space_create(&p->address_space)!=0)goto fail;

    for(uint64_t i=0;i<USER_STACK_PAGES;i++){
        uint64_t pa=pmm_alloc_page();
        if(!pa)goto fail;
        zero_page(pa);
        if(address_space_map(&p->address_space,USER_STACK_BASE+i*4096ULL,pa,
                             RIXURI_PTE_PRESENT|RIXURI_PTE_WRITE|RIXURI_PTE_USER|RIXURI_PTE_NX)!=0){
            pmm_free_page(pa);goto fail;
        }
    }

    rix_elf_image_t elf;
    if(elf_load_image(image,image_size,&p->address_space,&elf)!=0)goto fail;

    uint64_t kernel_stack=pmm_alloc_page();
    if(!kernel_stack)goto fail;
    zero_page(kernel_stack);
    p->kernel_stack=kernel_stack;
    p->kernel_stack_size=KERNEL_STACK_SIZE;
    tss_set_rsp0(kernel_stack + KERNEL_STACK_SIZE);
    p->state=RIX_PROC_SLEEPING;
    *out_entry=elf.entry;
    *out_user_stack=USER_STACK_TOP;
    return 0;
fail:
    p->state=RIX_PROC_ZOMBIE;
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
