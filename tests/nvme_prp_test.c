#include "../kernel/storage/nvme_prp.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

int main(void) {
    uint64_t pages[5] = {0x100000u, 0x101000u, 0x102000u, 0x103000u, 0x104000u};
    uint64_t list[512];
    uint64_t prp1=0, prp2=0;

    /* Single page. */
    assert(nvme_prp_build(pages[0], 0, 512, pages, 5, list, 512, 0x200000u, &prp1, &prp2)==0);
    assert(prp1==0x100000u && prp2==0u);
    printf("single PASS\n");

    /* Exact fit in first page stays single. */
    assert(nvme_prp_build(pages[0], 3584, 512, pages, 5, list, 512, 0x200000u, &prp1, &prp2)==0);
    assert(prp1==0x100000u+3584u && prp2==0u);
    printf("exact-fit PASS\n");
    /* Spanning two pages uses PRP2 direct. */
    assert(nvme_prp_build(pages[0], 3800, 512, pages, 5, list, 512, 0x200000u, &prp1, &prp2)==0);
    assert(prp1==0x100000u+3800u && prp2==0x101000u);
    printf("span-two PASS\n");

    /* Two pages via PRP2 direct. */
    assert(nvme_prp_build(pages[0], 0, 8192, pages, 5, list, 512, 0x200000u, &prp1, &prp2)==0);
    assert(prp1==0x100000u && prp2==0x101000u);
    printf("two-direct PASS\n");

    /* Three pages need list (off 4000 + 8192 = 3 pages). */
    assert(nvme_prp_build(pages[0], 4000, 8192, pages, 5, list, 512, 0x200000u, &prp1, &prp2)==0);
    assert(prp1==0x100000u+4000u && prp2==0x200000u);
    assert(list[0]==0x101000u && list[1]==0x102000u);
    printf("list-three PASS\n");

    /* Four pages (off 4000 + 12288 = 4 pages). */
    assert(nvme_prp_build(pages[0], 4000, 12288, pages, 5, list, 512, 0x200000u, &prp1, &prp2)==0);
    assert(prp2==0x200000u);
    assert(list[0]==0x101000u && list[1]==0x102000u && list[2]==0x103000u);
    printf("list-four PASS\n");

    /* Too few pages fails closed. */
    assert(nvme_prp_build(pages[0], 0, 8192, pages, 1, list, 512, 0x200000u, &prp1, &prp2)!=0);
    /* List too small fails. */
    assert(nvme_prp_build(pages[0], 4000, 8192, pages, 5, list, 1, 0x200000u, &prp1, &prp2)!=0);
    /* Bad alignment fails. */
    assert(nvme_prp_build(pages[0]+1, 0, 512, pages, 5, list, 512, 0x200000u, &prp1, &prp2)!=0);
    assert(nvme_prp_build(pages[0], 4096, 512, pages, 5, list, 512, 0x200000u, &prp1, &prp2)!=0);
    /* Huge page_count must be rejected before multiplication or page-table
     * traversal can wrap. */
    assert(nvme_prp_build(pages[0], 0, 512, pages, SIZE_MAX,
                          list, 512, 0x200000u, &prp1, &prp2)!=0);
    printf("negative PASS\n");

    printf("nvme prp tests: PASS\n");
    return 0;
}
