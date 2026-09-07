#pragma once
#include <stdint.h>
#include <stddef.h>
#define RIXURI_BOOT_MAGIC 0x52584955ULL
#define RIXURI_BOOT_VERSION 1u
typedef struct rixuri_boot_info {uint64_t magic;uint32_t version;uint32_t size;uint64_t memory_map;uint64_t memory_map_size;uint64_t memory_descriptor_size;uint32_t memory_descriptor_version;uint32_t reserved0;uint64_t rsdp;uint64_t kernel_phys_base;uint64_t kernel_phys_end;uint64_t framebuffer_base;uint64_t framebuffer_size;uint32_t framebuffer_width;uint32_t framebuffer_height;uint32_t framebuffer_pitch;uint32_t framebuffer_format;} rixuri_boot_info_t;
void serial_init(void);void serial_write(const char *s);void serial_write_n(const char *s,size_t length);void serial_write_hex(uint64_t value);void serial_write_dec(uint64_t value);void panic(const char *reason);void kernel_main(const rixuri_boot_info_t *boot);
void kernel_log(const char *s);void kernel_log_n(const char *s,size_t n);void kernel_log_hex(uint64_t v);void kernel_log_dec(uint64_t v);
