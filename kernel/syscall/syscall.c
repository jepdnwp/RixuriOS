#include "syscall.h"
#include <stdint.h>
#define MSR_STAR 0xC0000081u
#define MSR_LSTAR 0xC0000082u
#define MSR_FMASK 0xC0000084u
static inline void wrmsr(uint32_t m,uint64_t v){uint32_t lo=(uint32_t)v,hi=(uint32_t)(v>>32);__asm__ volatile("wrmsr"::"c"(m),"a"(lo),"d"(hi),"memory");}
static void syscall_entry(void) __attribute__((naked));
static void syscall_entry(void){__asm__ volatile("swapgs; sysretq");}
void syscall_init(void){wrmsr(MSR_LSTAR,(uint64_t)(uintptr_t)syscall_entry);wrmsr(MSR_FMASK,(1ULL<<9)|(1ULL<<10));}
