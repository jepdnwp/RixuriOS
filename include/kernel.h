#pragma once
#include <stdint.h>
#include <stddef.h>

#define RIXURI_BOOT_MAGIC 0x52584955ULL

typedef struct {
    uint64_t magic;
    uint64_t memory_map;
    uint64_t memory_map_size;
    uint64_t memory_descriptor_size;
    uint32_t memory_descriptor_version;
    uint64_t rsdp;
    uint64_t kernel_phys_base;
    uint64_t kernel_phys_end;
} rixuri_boot_info_t;

void serial_init(void);
void serial_write(const char *s);
void panic(const char *reason);
void kernel_main(const rixuri_boot_info_t *boot);
