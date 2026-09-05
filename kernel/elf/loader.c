#include "loader.h"
#include "../mm/pmm.h"
#include <stddef.h>
#include <stdint.h>

#define PAGE_SIZE 4096ULL
#define PT_LOAD 1U
#define USER_VA_MIN 0x0000008000000000ULL
#define USER_VA_MAX (1ULL << 47)

static int range(uint64_t a,uint64_t n,uint64_t lim){return a<=lim&&n<=lim-a;}
static void zero_page(uint8_t*p){for(size_t i=0;i<PAGE_SIZE;i++)p[i]=0;}
static int power_of_two(uint64_t v){return v&&(v&(v-1ULL))==0;}

int elf_load_image(const void*file,uint64_t file_size,rix_address_space_t*as,rix_elf_image_t*out){
 if(!file||!as||!out||elf64_validate(file,file_size)!=0)return -1;
 const uint8_t*b=(const uint8_t*)file;const Elf64_Ehdr*h=(const Elf64_Ehdr*)b;
 if(h->e_phnum==0||h->e_phentsize!=sizeof(Elf64_Phdr))return -1;
 if(h->e_phoff>file_size||!range(h->e_phoff,(uint64_t)h->e_phnum*sizeof(Elf64_Phdr),file_size))return -1;
 const Elf64_Phdr*ph=(const Elf64_Phdr*)(b+h->e_phoff);
 uint64_t lo=UINT64_MAX,hi=0,exec_lo=UINT64_MAX,exec_hi=0;int loads=0;

 /* Validate every PT_LOAD before changing the address space. */
 for(uint16_t i=0;i<h->e_phnum;i++){
  if(ph[i].p_type!=PT_LOAD)continue;
  loads++;
  if(ph[i].p_memsz==0||ph[i].p_filesz>ph[i].p_memsz)return -1;
  if(ph[i].p_vaddr<USER_VA_MIN||!range(ph[i].p_vaddr,ph[i].p_memsz,USER_VA_MAX))return -1;
  if(ph[i].p_offset>file_size||!range(ph[i].p_offset,ph[i].p_filesz,file_size))return -1;
  if(ph[i].p_align>1){
   if(!power_of_two(ph[i].p_align))return -1;
   if((ph[i].p_vaddr%ph[i].p_align)!=(ph[i].p_offset%ph[i].p_align))return -1;
  }
  uint64_t end=ph[i].p_vaddr+ph[i].p_memsz;if(end<ph[i].p_vaddr)return -1;
  if(ph[i].p_vaddr<lo)lo=ph[i].p_vaddr;if(end>hi)hi=end;
  if(ph[i].p_flags&1){if(ph[i].p_vaddr<exec_lo)exec_lo=ph[i].p_vaddr;if(end>exec_hi)exec_hi=end;}
 }
 if(!loads||lo==UINT64_MAX||exec_lo==UINT64_MAX||h->e_entry<exec_lo||h->e_entry>=exec_hi)return -1;

 for(uint16_t i=0;i<h->e_phnum;i++){
  if(ph[i].p_type!=PT_LOAD)continue;
  uint64_t end=ph[i].p_vaddr+ph[i].p_memsz;
  uint64_t first=ph[i].p_vaddr&~(PAGE_SIZE-1ULL),last=(end+PAGE_SIZE-1ULL)&~(PAGE_SIZE-1ULL);
  uint64_t wanted=RIXURI_PTE_PRESENT|RIXURI_PTE_USER|RIXURI_PTE_NX;
  if(ph[i].p_flags&2)wanted|=RIXURI_PTE_WRITE;
  if(ph[i].p_flags&1)wanted&=~RIXURI_PTE_NX;
  for(uint64_t va=first;va<last;va+=PAGE_SIZE){
   uint64_t old=address_space_query_flags(as,va);
   int owned=(old&(RIXURI_PTE_PRESENT|RIXURI_PTE_USER|RIXURI_PTE_OWNED))==
             (RIXURI_PTE_PRESENT|RIXURI_PTE_USER|RIXURI_PTE_OWNED);
   uint64_t pa=owned?(address_space_translate(as,va)&~(PAGE_SIZE-1ULL)):0;
   if(!owned){pa=pmm_alloc_page();if(!pa)return -1;zero_page((uint8_t*)(uintptr_t)pa);}
   if(address_space_map(as,va,pa,wanted)!=0){if(!owned)pmm_free_page(pa);return -1;}
   uint64_t ss=ph[i].p_vaddr>va?ph[i].p_vaddr:va,se=end<va+PAGE_SIZE?end:va+PAGE_SIZE;
   if(se>ss){
    uint64_t so=ss-ph[i].p_vaddr;
    if(so<ph[i].p_filesz){
     uint64_t n=ph[i].p_filesz-so;if(n>se-ss)n=se-ss;
     uint8_t*d=(uint8_t*)(uintptr_t)pa+(ss-va);const uint8_t*s=b+ph[i].p_offset+so;
     for(uint64_t k=0;k<n;k++)d[k]=s[k];
    }
   }
  }
 }
 out->entry=h->e_entry;out->image_lo=lo;out->image_hi=hi;return 0;
}
