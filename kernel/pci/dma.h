#pragma once
#include <stdint.h>
#include <stddef.h>

typedef struct { uint64_t physical; void *virtual_address; size_t pages; } rix_dma_buffer_t;

int pci_dma_alloc(size_t pages, uint64_t max_physical_exclusive, rix_dma_buffer_t *out);
void pci_dma_free(rix_dma_buffer_t *buffer);
