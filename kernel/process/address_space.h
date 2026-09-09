#pragma once
#include <stdint.h>

typedef struct { uint64_t pml4_phys; } rix_address_space_t;
int address_space_create(rix_address_space_t *as);
int address_space_clone(const rix_address_space_t *source, rix_address_space_t *destination);
int address_space_map(rix_address_space_t *as,uint64_t va,uint64_t pa,uint64_t flags);
int address_space_map_shared(rix_address_space_t *as,uint64_t va,uint64_t pa,uint64_t flags);
int address_space_update_flags(rix_address_space_t *as,uint64_t va,uint64_t flags);
int address_space_unmap(rix_address_space_t *as,uint64_t va);
uint64_t address_space_translate(const rix_address_space_t *as,uint64_t va);
uint64_t address_space_query_flags(const rix_address_space_t *as,uint64_t va);
void address_space_destroy(rix_address_space_t *as);
/* Mirror kernel PML4 slots into a process PML4 (private user slots untouched).
 * Slots PML4[1] (user image) and PML4[255] (user stack) are user-exclusive
 * and never touched; any other slot carrying the USER bit is user-owned and
 * wins over the kernel value. Everything else tracks the live kernel
 * template (USER|OWNED cleared, NX/W^X preserved), so kernel mappings created
 * at any time (high MMIO BARs, LAPIC, IOAPIC) are visible after the next
 * switch. Returns rewritten slot count, -1 on bad input. Call right before
 * loading the address space into CR3. */
int address_space_sync_kernel(rix_address_space_t *as);
