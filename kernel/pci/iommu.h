#pragma once
#include <stdint.h>
#include <stddef.h>

int iommu_init(void);
int iommu_available(void);
int iommu_map(uint64_t device_id, uint64_t physical, uint64_t length, uint64_t permissions);
int iommu_unmap(uint64_t device_id, uint64_t physical, uint64_t length);
