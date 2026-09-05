#include "loader.h"
#include "../mm/pmm.h"
#include <stddef.h>
#include <stdint.h>

static int range(uint64_t a,uint64_t n,uint64_t lim){return a<=lim&&n<=lim-a;}
static void zero_page(uint8_t*p){for(size_t i=0;i<4096;i++)p[i]=0;}

int elf_load_image(const void*file,uint64_t file_size,rix_address_space_t*as,rix_elf_image_t*out){
 if(!file||!as||!out||elf64_validate(file,file_size)!=0)return -1;
 const uint8_t*b=(const uint8_t*)file;const Elf64_Ehdr*h=(const Elf64_Ehdr*)b;
 const Elf64_Phdr*ph=(const Elf64_Phdr*)(b+h->e_phoff);uint64_t lo=UINT64_MAX,hi=0;
 for(uint16_t i=0;i<h->e_phnum;i++){
  if(ph[i].p_type!=1)continue;
  if(ph[i].p_memsz==0||ph[i].p_vaddr<0x1000||!range(ph[i].p_vaddr,ph[i].p_memsz,1ULL<<47)||ph[i].p_offset>file_size||ph[i].p_filesz>file_size-ph[i].p_offset)return -1;
  uint64_t end=ph[i].p_vaddr+ph[i].p_memsz;if(end<ph[i].p_vaddr)return -1;
  uint64_t first=ph[i].p_vaddr&~0xfffULL,last=(end+0xfffULL)&~0xfffULL;if(last<end)return -1;
  uint64_t flags=RIXURI_PTE_PRESENT|RIXURI_PTE_USER|RIXURI_PTE_NX;if(ph[i].p_flags&2)flags|=RIXURI_PTE_WRITE;if(ph[i].p_flags&1)flags&=~RIXURI_PTE_NX;
  for(uint64_t va=first;va<last;va+=4096){
   uint64_t pa=address_space_translate(as,va)&~0xfffULL;
   uint64_t old_flags=address_space_query_flags(as,va);
   if(!pa){
    pa=pmm_alloc_page();if(!pa)return -1;zero_page((uint8_t*)(uintptr_t)pa);
   }else{
    flags|=old_flags&(RIXURI_PTE_WRITE|RIXURI_PTE_NX);
    if(address_space_map(as,va,pa,flags)!=0)return -1;
   }
   if(address_space_map(as,va,pa,flags)!=0){if(!old_flags)pmm_free_page(pa);return -1;}
   uint64_t ss=ph[i].p_vaddr>va?ph[i].p_vaddr:va,se=end<va+4096?end:va+4096;
   if(se>ss){uint64_t so=ss-ph[i].p_vaddr;if(so<ph[i].p_filesz){uint64_t n=ph[i].p_filesz-so;if(n>se-ss)n=se-ss;uint8_t*d=(uint8_t*)(uintptr_t)pa+(ss-va);const uint8_t*s=b+ph[i].p_offset+so;for(uint64_t k=0;k<n;k++)d[k]=s[k];}}
  }
  if(ph[i].p_vaddr<lo)lo=ph[i].p_vaddr;if(end>hi)hi=end;
 }
 if(lo==UINT64_MAX||h->e_entry<lo||h->e_entry>=hi)return -1;
 out->entry=h->e_entry;out->image_lo=lo;out->image_hi=hi;return 0;
}
