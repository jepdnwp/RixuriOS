#pragma once
#include <stdint.h>

typedef struct { uint64_t pml4_phys; } rix_address_space_t;
int address_space_create(rix_address_space_t *as);
int address_space_map(rix_address_space_t *as,uint64_t va,uint64_t pa,uint64_t flags);
void address_space_destroy(rix_address_space_t *as);
