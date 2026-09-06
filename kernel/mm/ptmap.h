#ifndef RIXURI_PTMAP_H
#define RIXURI_PTMAP_H

#include <stdint.h>

void *pt_kmap(uint64_t physical_page);
void pt_kunmap(void *kernel_address);
uint64_t pt_read(uint64_t physical_page, unsigned index);
void pt_write(uint64_t physical_page, unsigned index, uint64_t value);

#endif

