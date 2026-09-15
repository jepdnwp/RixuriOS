#pragma once
#include <stddef.h>
#include <stdint.h>

/* Pure PRP builder (no HW, no VMM): given the translated page addresses
 * covering [first_pa-first_off, +bytes), produce PRP1/PRP2 (+ list).
 * page_phys[0] must equal first_pa & ~0xFFF (first page base); page_count
 * is the number of pages in page_phys. list is a caller-owned page-sized
 * buffer for the PRP list when >2 pages are needed; list_phys is its device
 * address. Fails closed when pages are insufficient or list is too small. */
int nvme_prp_build(uint64_t first_pa, uint64_t first_off, uint64_t bytes,
                   const uint64_t *page_phys, size_t page_count,
                   uint64_t *list, size_t list_cap,
                   uint64_t list_phys,
                   uint64_t *out_prp1, uint64_t *out_prp2);
