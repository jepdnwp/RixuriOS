#pragma once
#include <stdint.h>

typedef struct __attribute__((packed)) {
    uint32_t reserved0;
    uint64_t rsp0, rsp1, rsp2;
    uint64_t reserved1;
    uint64_t ist[7];
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} x86_tss_t;

void tss_init(void);
const x86_tss_t *tss_current(void);
