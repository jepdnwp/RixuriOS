#pragma once
#include <stdint.h>
#include <stddef.h>

#define RIX_DMA_MAX_PAGES 256

typedef struct { uint64_t pages[RIX_DMA_MAX_PAGES]; size_t count; } rix_dma_buffer_t;

int pci_dma_alloc(size_t pages, uint64_t max_physical_exclusive, rix_dma_buffer_t *out);
void pci_dma_free(rix_dma_buffer_t *buffer);
