#pragma once
#include <stddef.h>
#include <stdint.h>
#define RIX_ELF_NIDENT 16
#define RIX_ELFCLASS64 2
#define RIX_ELFDATA2LSB 1
#define RIX_ELF_MACHINE_X86_64 62
#define RIX_PT_LOAD 1
#define RIX_PT_DYNAMIC 2
#define RIX_PF_X 1
#define RIX_PF_W 2
#define RIX_PF_R 4

typedef struct {unsigned char ident[RIX_ELF_NIDENT];uint16_t type;uint16_t machine;uint32_t version;uint64_t entry;uint64_t phoff;uint64_t shoff;uint32_t flags;uint16_t ehsize;uint16_t phentsize;uint16_t phnum;uint16_t shentsize;uint16_t shnum;uint16_t shstrndx;} rix_elf64_ehdr_t;
typedef struct {uint32_t type;uint32_t flags;uint64_t offset;uint64_t vaddr;uint64_t paddr;uint64_t filesz;uint64_t memsz;uint64_t align;} rix_elf64_phdr_t;
int elf64_validate(const void *image,size_t image_size,rix_elf64_ehdr_t *out);
int elf64_program_header(const void *image,size_t image_size,uint16_t index,rix_elf64_phdr_t *out);
