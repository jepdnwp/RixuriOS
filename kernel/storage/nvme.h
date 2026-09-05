#pragma once
#include <stddef.h>
#include <stdint.h>

typedef struct { uint8_t bus,device,function; uint64_t bar0; uint64_t cap; uint32_t version; uint32_t cc; uint32_t csts; uint16_t mqes; uint8_t dstrd; uint8_t css; } rix_nvme_controller_t;
int nvme_init(void);
size_t nvme_controller_count(void);
const rix_nvme_controller_t *nvme_controller(size_t index);
