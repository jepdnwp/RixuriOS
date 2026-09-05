#pragma once
#include <stdint.h>

typedef struct { uint64_t pml4_phys; } rix_address_space_t;
int address_space_create(rix_address_space_t *as);
int address_space_map(rix_address_space_t *as,uint64_t va,uint64_t pa,uint64_t flags);
uint64_t address_space_translate(const rix_address_space_t *as,uint64_t va);
uint64_t address_space_query_flags(const rix_address_space_t *as,uint64_t va);
void address_space_destroy(rix_address_space_t *as);
