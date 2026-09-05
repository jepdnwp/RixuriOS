#pragma once
#include <stddef.h>
#include <stdint.h>

#define RIX_NVME_MAX_NAMESPACES 32

typedef struct {
    uint8_t used;
    uint32_t nsid;
    uint64_t size_lba;
    uint64_t capacity_lba;
    uint64_t utilization_lba;
    uint32_t lba_size;
    uint8_t lba_format;
} rix_nvme_namespace_t;

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
    uint8_t io_ready;
    uint8_t identify_valid;
    uint8_t io_failed;
    uint16_t admin_sq_tail;
    uint16_t admin_cq_head;
    uint8_t admin_cq_phase;
    uint16_t admin_cid;
    uint16_t io_sq_tail;
    uint16_t io_cq_head;
    uint8_t io_cq_phase;
    uint16_t io_cid;
    uint64_t admin_sq_phys;
    uint64_t admin_cq_phys;
    uint64_t io_sq_phys;
    uint64_t io_cq_phys;
    uint16_t io_queue_depth;
    char serial[21];
    char model[41];
    char firmware[9];
    uint32_t nn;
    rix_nvme_namespace_t namespaces[RIX_NVME_MAX_NAMESPACES];
} rix_nvme_controller_t;

int nvme_init(void);
size_t nvme_controller_count(void);
const rix_nvme_controller_t *nvme_controller(size_t index);
int nvme_identify_controller(size_t index);
int nvme_identify_namespace(size_t index, uint32_t nsid);
int nvme_read(size_t controller, uint32_t nsid, uint64_t lba, uint32_t count, void *buffer, size_t buffer_size);
int nvme_write(size_t controller, uint32_t nsid, uint64_t lba, uint32_t count, const void *buffer, size_t buffer_size);
int nvme_flush(size_t controller, uint32_t nsid);
