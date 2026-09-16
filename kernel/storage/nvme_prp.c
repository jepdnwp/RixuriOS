#include "nvme_prp.h"

#define PRP_PAGE 4096ULL

int nvme_prp_build(uint64_t first_pa, uint64_t first_off, uint64_t bytes,
                   const uint64_t *page_phys, size_t page_count,
                   uint64_t *list, size_t list_cap,
                   uint64_t list_phys,
                   uint64_t *out_prp1, uint64_t *out_prp2) {
    if (!page_phys || !page_count || !out_prp1 || !out_prp2) return -1;
    if (first_off >= PRP_PAGE || !bytes) return -1;
    if (page_count > UINT64_MAX / PRP_PAGE ||
        bytes > (uint64_t)page_count * PRP_PAGE) return -1;
    if ((first_pa & 0xFFFULL) != 0) return -1;
    if (page_phys[0] != first_pa) return -1;
    for (size_t i = 0; i < page_count; i++) {
        if (!page_phys[i] || (page_phys[i] & 0xFFFULL)) return -1;
    }
    uint64_t first_bytes = PRP_PAGE - first_off;
    if (first_pa > UINT64_MAX - first_off) return -1;
    *out_prp1 = first_pa + first_off;
    if (bytes <= first_bytes) {
        *out_prp2 = 0;
        return 0;
    }
    uint64_t remain = bytes - first_bytes;
    size_t need_pages = (size_t)(remain / PRP_PAGE);
    if (remain % PRP_PAGE) {
        if (need_pages == SIZE_MAX) return -1;
        need_pages++;
    }
    if (need_pages >= page_count) return -1;
    if (need_pages == 1) {
        *out_prp2 = page_phys[1];
        return 0;
    }
    if (!list || !list_cap || !list_phys || (list_phys & 0xFFFULL)) return -1;
    if (need_pages > list_cap) return -1;
    for (size_t i = 0; i < need_pages; i++) list[i] = page_phys[1 + i];
    *out_prp2 = list_phys;
    return 0;
}
