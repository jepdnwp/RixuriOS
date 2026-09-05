#pragma once
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t bus, device, function;
    uint64_t bar0;
    uint64_t cap;
    uint32_t version;
    uint32_t cc;
    uint32_t csts;
    uint16_t mqes;
    uint8_t dstrd;
    uint8_t css;
    uint8_t admin_ready;
    uint8_t identify_valid;
    uint32_t serial[5];
    uint32_t model[10];
    uint32_t firmware[2];
    uint32_t nn;
} rix_nvme_controller_t;

int nvme_init(void);
size_t nvme_controller_count(void);
const rix_nvme_controller_t *nvme_controller(size_t index);
int nvme_identify_controller(size_t index);
