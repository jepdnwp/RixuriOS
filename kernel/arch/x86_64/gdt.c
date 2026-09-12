#include "gdt.h"
#include "tss.h"
#include <stdint.h>
#include <stddef.h>
struct gdt_ptr{uint16_t limit;uint64_t base;}__attribute__((packed));
#define GDT_KCODE 0x00AF9A000000FFFFULL
#define GDT_KDATA 0x00CF92000000FFFFULL
#define GDT_UCODE 0x00AFFA000000FFFFULL
#define GDT_UDATA 0x00CFF2000000FFFFULL
static uint64_t gdt[7]__attribute__((aligned(8)));static x86_tss_t tss __attribute__((aligned(16)));static uint8_t double_fault_stack[4096]__attribute__((aligned(16)));
/* Phase C2 registry: AP TSS pointers (physmap VAs registered at bringup).
 * The BSP never registers and keeps the static TSS above. */
static x86_tss_t*cpu_tss[TSS_MAX_CPUS];
__attribute__((weak)) int tss_cpu_index(void){return -1;}
static void set_tss_desc_in(uint64_t*g,unsigned i,uint64_t base,uint32_t limit){uint64_t lo=(limit&0xffffULL)|((base&0xffffffULL)<<16)|(0x89ULL<<40)|(((limit>>16)&0xfULL)<<48)|(((base>>24)&0xffULL)<<56);g[i]=lo;g[i+1]=base>>32;}
static void set_tss_desc(unsigned i,uint64_t base,uint32_t limit){set_tss_desc_in(gdt,i,base,limit);}
int gdt_build_cpu_copy(uint64_t out[GDT_CPU_COPY_ENTRIES],uint64_t tss_base,uint32_t tss_limit){if(!out||!tss_base)return -1;out[0]=0;out[1]=GDT_KCODE;out[2]=GDT_KDATA;out[3]=GDT_UCODE;out[4]=GDT_UDATA;set_tss_desc_in(out,5,tss_base,tss_limit);return 0;}
int tss_register_cpu(int idx,x86_tss_t*tss_ptr){if(idx<0||idx>=TSS_MAX_CPUS||!tss_ptr)return -1;cpu_tss[idx]=tss_ptr;return 0;}
static x86_tss_t*tss_select(void){int idx=tss_cpu_index();if(idx>=0&&idx<TSS_MAX_CPUS&&cpu_tss[idx])return cpu_tss[idx];return &tss;}
static void gdt_load(const struct gdt_ptr*p){__asm__ volatile("lgdt (%0)"::"r"(p):"memory");__asm__ volatile("pushq $0x08;leaq 1f(%%rip),%%rax;pushq %%rax;lretq;1:;movw $0x10,%%ax;movw %%ax,%%ds;movw %%ax,%%es;movw %%ax,%%ss;xor %%ax,%%ax;movw %%ax,%%fs;movw %%ax,%%gs":::"rax","memory");}
void tss_init(void){for(size_t i=0;i<sizeof(tss)/sizeof(uint64_t);i++)((uint64_t*)&tss)[i]=0;tss.ist[0]=(uint64_t)(uintptr_t)(double_fault_stack+sizeof(double_fault_stack));tss.iomap_base=(uint16_t)sizeof(tss);uint16_t sel=0x28;__asm__ volatile("ltr %0"::"r"(sel):"memory");}
void tss_set_rsp0(uint64_t stack_top){tss_select()->rsp0=stack_top;}
const x86_tss_t*tss_current(void){return tss_select();}
void gdt_init(void){for(size_t i=0;i<7;i++)gdt[i]=0;gdt[1]=GDT_KCODE;gdt[2]=GDT_KDATA;gdt[3]=GDT_UCODE;gdt[4]=GDT_UDATA;set_tss_desc(5,(uint64_t)(uintptr_t)&tss,sizeof(tss)-1);struct gdt_ptr p={(uint16_t)(sizeof(gdt)-1),(uint64_t)(uintptr_t)gdt};gdt_load(&p);tss_init();}
_Static_assert(GDT_CPU_TSS_OFF%16==0,"per-CPU TSS offset breaks 16-alignment");
_Static_assert(GDT_CPU_TSS_OFF+sizeof(x86_tss_t)<=4096,"per-CPU TSS overflows its page");
