#include "loader.h"
#include "../mm/pmm.h"
#include "../mm/vmm.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static int range(uint64_t a,uint64_t n,uint64_t lim){return n<=lim-a;}
int elf_load_image(const void *file,uint64_t file_size,rix_elf_image_t *out){
 if(!file||!out||elf64_validate(file,file_size)!=0)return -1;
 const Elf64_Ehdr*h=(const Elf64_Ehdr*)file;const Elf64_Phdr*ph=(const Elf64_Phdr*)((const uint8_t*)file+h->e_phoff);
 uint64_t lo=UINT64_MAX,hi=0;
 for(uint16_t i=0;i<h->e_phnum;i++) if(ph[i].p_type==1){
   if(!range(ph[i].p_vaddr,ph[i].p_memsz,1ULL<<47)||ph[i].p_vaddr<0x1000)return -1;
   uint64_t first=ph[i].p_vaddr&~0xfffULL,last=(ph[i].p_vaddr+ph[i].p_memsz+0xfffULL)&~0xfffULL;
   for(uint64_t va=first;va<last;va+=4096){uint64_t pa=pmm_alloc_page();if(!pa)return -1;for(size_t j=0;j<4096;j++)((uint8_t*)(uintptr_t)pa)[j]=0;uint64_t flags=RIXURI_PTE_PRESENT|RIXURI_PTE_USER|RIXURI_PTE_NX;if(ph[i].p_flags&2)flags|=RIXURI_PTE_WRITE;if(ph[i].p_flags&1)flags&=~RIXURI_PTE_NX;if(address_space_map((rix_address_space_t*)0,va,pa,flags)!=0){pmm_free_page(pa);return -1;}}
   if(ph[i].p_vaddr<lo)lo=ph[i].p_vaddr;if(ph[i].p_vaddr+ph[i].p_memsz>hi)hi=ph[i].p_vaddr+ph[i].p_memsz;
 }
 out->entry=h->e_entry;out->image_lo=lo;out->image_hi=hi;return lo==UINT64_MAX?-1:0;
}
