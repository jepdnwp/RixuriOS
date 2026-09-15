#pragma once
#include <stddef.h>
#include <stdint.h>
#define RIX_SYSINFO_VERSION 1u
typedef struct { uint32_t version; uint32_t struct_size; uint32_t page_size; uint32_t flags; uint64_t total_pages; uint64_t free_pages; uint64_t reserved_pages; uint64_t uptime_sec; uint32_t uptime_nsec; uint32_t pad; uint64_t reserved0; } rix_sysinfo_t;
_Static_assert(sizeof(rix_sysinfo_t)==64,"sysinfo layout must stay 64 bytes");
int sysinfo(rix_sysinfo_t *out);
