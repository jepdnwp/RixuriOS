#include "elf.h"
#include <stddef.h>
#include <stdint.h>
static int range_ok(size_t size,uint64_t off,uint64_t len){if(off>size)return 0;if(len>size-off)return 0;return 1;}
static void copy_bytes(void *dst,const void *src,size_t n){uint8_t*d=dst;const uint8_t*s=src;for(size_t i=0;i<n;i++)d[i]=s[i];}
int elf64_validate(const void *image,size_t image_size,rix_elf64_ehdr_t *out){
    if(!image||image_size<sizeof(rix_elf64_ehdr_t)||!out)return -1;
    copy_bytes(out,image,sizeof(*out));
    if(out->ident[0]!=0x7f||out->ident[1]!='E'||out->ident[2]!='L'||out->ident[3]!='F')return -1;
    if(out->ident[4]!=RIX_ELFCLASS64||out->ident[5]!=RIX_ELFDATA2LSB||out->ident[6]!=1)return -1;
    if(out->machine!=RIX_ELF_MACHINE_X86_64||out->version!=1)return -1;
    if(out->ehsize!=sizeof(rix_elf64_ehdr_t)||out->phentsize!=sizeof(rix_elf64_phdr_t)||out->phnum==0)return -1;
    uint64_t phbytes=(uint64_t)out->phentsize*(uint64_t)out->phnum;if(out->phnum&&phbytes/out->phnum!=out->phentsize)return -1;
    if(!range_ok(image_size,out->phoff,phbytes))return -1;
    for(uint16_t i=0;i<out->phnum;i++){rix_elf64_phdr_t p;if(elf64_program_header(image,image_size,i,&p)!=0)return -1;if(p.type==RIX_PT_LOAD){if(p.filesz>p.memsz)return -1;if(!range_ok(image_size,p.offset,p.filesz))return -1;if(p.vaddr+p.memsz<p.vaddr)return -1;if(p.align&&((p.align&(p.align-1))!=0))return -1;if((p.flags&RIX_PF_W)&&(p.flags&RIX_PF_X))return -1;}}
    return 0;
}
int elf64_program_header(const void *image,size_t image_size,uint16_t index,rix_elf64_phdr_t *out){if(!image||!out||image_size<sizeof(rix_elf64_ehdr_t))return -1;rix_elf64_ehdr_t h;copy_bytes(&h,image,sizeof(h));if(h.phentsize!=sizeof(rix_elf64_phdr_t)||index>=h.phnum)return -1;uint64_t off=h.phoff+(uint64_t)index*h.phentsize;if(off<h.phoff||!range_ok(image_size,off,sizeof(rix_elf64_phdr_t)))return -1;copy_bytes(out,(const uint8_t*)image+off,sizeof(*out));return 0;}
