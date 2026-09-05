#include "iommu.h"

/*
 * Hardware IOMMU programming is intentionally kept separate from the DMA
 * allocator. Until DMAR/IVRS parsing and a real translation domain exist,
 * returning unavailable is safer than pretending that isolation is active.
 */
static int available;

int iommu_init(void){available=0;return 0;}
int iommu_available(void){return available;}
int iommu_map(uint64_t device_id,uint64_t physical,uint64_t length,uint64_t permissions){
    (void)device_id;(void)physical;(void)length;(void)permissions;
    return available?0:-1;
}
int iommu_unmap(uint64_t device_id,uint64_t physical,uint64_t length){
    (void)device_id;(void)physical;(void)length;
    return available?0:-1;
}
