#pragma once
#include <stdint.h>
#include "pci.h"

#define RIX_MSIX_MAX_VECTORS 2048

typedef struct { uint64_t address; uint32_t data; uint32_t vector_control; } rix_msix_entry_t;
int pci_msix_enable(const rix_pci_device_t *device, unsigned vector);
int pci_msix_disable(const rix_pci_device_t *device);
int pci_msix_set_entry(const rix_pci_device_t *device, unsigned vector, uint64_t address, uint32_t data, int masked);
