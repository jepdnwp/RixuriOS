#include "gdt.h"
#include "tss.h"
#include <stdint.h>
#include <stddef.h>
struct gdt_ptr{uint16_t limit;uint64_t base;}__attribute__((packed));
static uint64_t gdt[7]__attribute__((aligned(8)));static x86_tss_t tss __attribute__((aligned(16)));
static void set_tss_desc(unsigned i,uint64_t base,uint32_t limit){uint64_t lo=(limit&0xffffULL)|((base&0xffffffULL)<<16)|(0x89ULL<<40)|(((limit>>16)&0xfULL)<<48)|(((base>>24)&0xffULL)<<56);gdt[i]=lo;gdt[i+1]=base>>32;}
static void gdt_load(const struct gdt_ptr*p){__asm__ volatile("lgdt (%0)"::"r"(p):"memory");__asm__ volatile("pushq $0x08;leaq 1f(%%rip),%%rax;pushq %%rax;lretq;1:;movw $0x10,%%ax;movw %%ax,%%ds;movw %%ax,%%es;movw %%ax,%%ss":::"rax","memory");}
void tss_init(void){for(size_t i=0;i<sizeof(tss)/sizeof(uint64_t);i++)((uint64_t*)&tss)[i]=0;tss.iomap_base=(uint16_t)sizeof(tss);uint16_t sel=0x28;__asm__ volatile("ltr %0"::"r"(sel):"memory");}
void tss_set_rsp0(uint64_t stack_top){tss.rsp0=stack_top;}
const x86_tss_t*tss_current(void){return &tss;}
void gdt_init(void){for(size_t i=0;i<7;i++)gdt[i]=0;gdt[1]=0x00AF9A000000FFFFULL;gdt[2]=0x00CF92000000FFFFULL;gdt[3]=0x00AFFA000000FFFFULL;gdt[4]=0x00CFF2000000FFFFULL;set_tss_desc(5,(uint64_t)(uintptr_t)&tss,sizeof(tss)-1);struct gdt_ptr p={(uint16_t)(sizeof(gdt)-1),(uint64_t)(uintptr_t)gdt};gdt_load(&p);tss_init();}
