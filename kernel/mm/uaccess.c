#include "uaccess.h"
#include "vmm.h"
#include <stddef.h>
#include <stdint.h>

#define USER_MAX48 ((1ULL<<47)-1ULL)

static int range_ok(uint64_t address,size_t length){if(length==0)return 1;if(address>USER_MAX48)return 0;uint64_t last=address+(uint64_t)length-1ULL;if(last<address||last>USER_MAX48)return 0;return 1;}
int user_range_valid(uint64_t address,size_t length,int write){if(!range_ok(address,length))return -1;uint64_t end=address+(length?length-1:0);for(uint64_t page=address&~0xFFFULL;;page+=0x1000ULL){uint64_t flags=vmm_query_flags(page);if(!(flags&RIXURI_PTE_PRESENT)||(flags&RIXURI_PTE_USER)==0||(write&&!(flags&RIXURI_PTE_WRITE)))return -1;if(page>=(end&~0xFFFULL))break;}return 0;}
int copy_from_user(void *kernel_dst,uint64_t user_src,size_t length){if(!kernel_dst||user_range_valid(user_src,length,0)!=0)return -1;uint8_t *d=(uint8_t*)kernel_dst;uint64_t s=user_src;for(size_t i=0;i<length;i++)d[i]=*(volatile uint8_t *)(uintptr_t)(s+i);return 0;}
int copy_to_user(uint64_t user_dst,const void *kernel_src,size_t length){if(!kernel_src||user_range_valid(user_dst,length,1)!=0)return -1;const uint8_t *s=(const uint8_t*)kernel_src;uint8_t *d=(uint8_t *)(uintptr_t)user_dst;for(size_t i=0;i<length;i++)d[i]=s[i];return 0;}
