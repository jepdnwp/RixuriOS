#pragma once
#include <stdint.h>
#include "elf.h"

typedef struct { uint64_t entry; uint64_t image_lo; uint64_t image_hi; } rix_elf_image_t;
int elf_load_image(const void *file,uint64_t file_size,rix_elf_image_t *out);
