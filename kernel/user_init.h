#pragma once
#include <stdint.h>

extern const uint8_t _binary_build_user_init_elf_start[];
extern const uint8_t _binary_build_user_init_elf_end[];

static inline const void *rixuri_user_init_image(void){return (const void *)_binary_build_user_init_elf_start;}
static inline uint64_t rixuri_user_init_image_size(void){return (uint64_t)(_binary_build_user_init_elf_end-_binary_build_user_init_elf_start);}
