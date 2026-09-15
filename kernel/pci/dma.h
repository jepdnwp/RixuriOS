#pragma once
#include <stdint.h>
#include <stddef.h>

#define RIX_DMA_MAX_PAGES 256
#define RIX_DMA_MAX_MAPPINGS 64

typedef struct { uint64_t pages[RIX_DMA_MAX_PAGES]; size_t count; } rix_dma_buffer_t;

typedef enum { RIX_DMA_TO_DEVICE = 0, RIX_DMA_FROM_DEVICE = 1, RIX_DMA_BIDIRECTIONAL = 2 } rix_dma_direction_t;

int pci_dma_alloc(size_t pages, uint64_t max_physical_exclusive, rix_dma_buffer_t *out);
void pci_dma_free(rix_dma_buffer_t *buffer);
/* Central DMA ownership contract (Phase 10 P0).
 * Map validates every page in [phys, phys+size) is PMM-managed, in-use and
 * not reserved, records owner+direction, rejects overlapping active maps,
 * and returns the device address (identity without IOMMU). Unmap requires
 * an exact active entry. SG maps an array of page addresses (each 4K).
 * Sync orders CPU/device visibility (mfence on target, barrier on host).
 * Bounce allocates below max_exclusive when the source sits above it and
 * copies TO_DEVICE on map / FROM_DEVICE on unmap.
 * Owner 0 is never valid. All functions fail closed with -1. */
int pci_dma_map(uint64_t phys, size_t size, rix_dma_direction_t dir, uint64_t owner, uint64_t *out_dev_addr);
int pci_dma_unmap(uint64_t dev_addr, size_t size);
int pci_dma_map_sg(const uint64_t *phys_list, size_t count, rix_dma_direction_t dir, uint64_t owner, uint64_t *out_dev_addrs);
int pci_dma_is_mapped(uint64_t phys, size_t size);
void pci_dma_sync_for_device(uint64_t dev_addr, size_t size);
void pci_dma_sync_for_cpu(uint64_t dev_addr, size_t size);
int pci_dma_needs_bounce(uint64_t phys, size_t size, uint64_t max_exclusive);
int pci_dma_map_bounce(uint64_t phys, size_t size, rix_dma_direction_t dir, uint64_t owner, uint64_t max_exclusive, uint64_t *out_dev_addr);
int pci_dma_unmap_bounce(uint64_t dev_addr, size_t size, rix_dma_direction_t dir);
