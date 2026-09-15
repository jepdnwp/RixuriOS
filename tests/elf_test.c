#include "../kernel/elf/elf.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

static void build_good(uint8_t *img, size_t *out_size, uint64_t entry) {
    memset(img, 0, 1024);
    rix_elf64_ehdr_t *h = (rix_elf64_ehdr_t *)img;
    h->ident[0]=0x7f; h->ident[1]='E'; h->ident[2]='L'; h->ident[3]='F';
    h->ident[4]=RIX_ELFCLASS64; h->ident[5]=RIX_ELFDATA2LSB; h->ident[6]=1;
    h->type=2; h->machine=RIX_ELF_MACHINE_X86_64; h->version=1;
    h->entry=entry; h->phoff=sizeof(*h);
    h->ehsize=sizeof(*h); h->phentsize=sizeof(rix_elf64_phdr_t); h->phnum=2;
    rix_elf64_phdr_t *p=(rix_elf64_phdr_t *)(img+h->phoff);
    p[0].type=RIX_PT_LOAD; p[0].flags=RIX_PF_R|RIX_PF_X;
    p[0].offset=0x100; p[0].vaddr=0x400000; p[0].filesz=0x100; p[0].memsz=0x100; p[0].align=0x1000;
    p[1].type=RIX_PT_LOAD; p[1].flags=RIX_PF_R|RIX_PF_W;
    p[1].offset=0x200; p[1].vaddr=0x401000; p[1].filesz=0x80; p[1].memsz=0x100; p[1].align=0x1000;
    *out_size=1024;
}

int main(void) {
    uint8_t img[1024]; size_t sz=0;
    rix_elf64_ehdr_t out;

    build_good(img,&sz,0x400010);
    assert(elf64_validate(img,sz,&out)==0);
    printf("good PASS\n");

    /* Overlapping PT_LOAD must be rejected. */
    build_good(img,&sz,0x400010);
    { rix_elf64_phdr_t *p=(rix_elf64_phdr_t *)(img+sizeof(rix_elf64_ehdr_t));
      p[1].vaddr=0x400080; }
    assert(elf64_validate(img,sz,&out)!=0);
    printf("overlap-reject PASS\n");

    /* Entry in non-executable segment must be rejected. */
    build_good(img,&sz,0x401010);
    assert(elf64_validate(img,sz,&out)!=0);
    printf("entry-nx-reject PASS\n");

    /* Entry outside all segments must be rejected. */
    build_good(img,&sz,0x500000);
    assert(elf64_validate(img,sz,&out)!=0);
    printf("entry-outside-reject PASS\n");

    /* W+X segment must be rejected. */
    build_good(img,&sz,0x400010);
    { rix_elf64_phdr_t *p=(rix_elf64_phdr_t *)(img+sizeof(rix_elf64_ehdr_t));
      p[0].flags=RIX_PF_R|RIX_PF_W|RIX_PF_X; }
    assert(elf64_validate(img,sz,&out)!=0);
    printf("wx-reject PASS\n");

    /* filesz > memsz must be rejected. */
    build_good(img,&sz,0x400010);
    { rix_elf64_phdr_t *p=(rix_elf64_phdr_t *)(img+sizeof(rix_elf64_ehdr_t));
      p[0].filesz=0x200; p[0].memsz=0x100; }
    assert(elf64_validate(img,sz,&out)!=0);
    printf("filesz-reject PASS\n");

    /* Bad magic / class must be rejected. */
    build_good(img,&sz,0x400010);
    img[0]=0x00;
    assert(elf64_validate(img,sz,&out)!=0);
    printf("magic-reject PASS\n");

    /* Adjacent (non-overlapping) segments must be accepted. */
    build_good(img,&sz,0x400010);
    { rix_elf64_phdr_t *p=(rix_elf64_phdr_t *)(img+sizeof(rix_elf64_ehdr_t));
      p[1].vaddr=0x400100; }
    assert(elf64_validate(img,sz,&out)==0);
    printf("adjacent-accept PASS\n");

    printf("elf tests: PASS\n");
    return 0;
}
