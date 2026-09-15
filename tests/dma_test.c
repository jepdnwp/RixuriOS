#include "../kernel/pci/dma.h"
#include "../kernel/mm/pmm.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

static unsigned lockdep_warnings;
void serial_write(const char *s) { (void)s; ++lockdep_warnings; }

static void put32(uint8_t *e, size_t o, uint32_t v) { memcpy(e+o,&v,sizeof(v)); }
static void put64(uint8_t *e, size_t o, uint64_t v) { memcpy(e+o,&v,sizeof(v)); }

int main(void) {
    uint8_t map[40];
    memset(map,0,sizeof(map));
    put32(map,0u,7u);
    put64(map,8u,0u);
    put64(map,24u,512u);
    pmm_init(map,sizeof(map),sizeof(map),0x100000u,0x101000u,0x102000u,0x1000u);

    /* Alloc below 4G stays low. */
    rix_dma_buffer_t buf;
    assert(pci_dma_alloc(2, 0x100000000ULL, &buf)==0);
    assert(buf.count==2);
    for(size_t i=0;i<buf.count;i++) assert(buf.pages[i] < 0x100000000ULL);
    printf("alloc-low PASS\n");

    /* Map owned buffer with direction + owner. */
    uint64_t dev=0;
    assert(pci_dma_map(buf.pages[0],4096u,RIX_DMA_TO_DEVICE,0xA1u,&dev)==0);
    assert(dev==buf.pages[0]);
    assert(pci_dma_is_mapped(buf.pages[0],4096u));
    printf("map PASS\n");

    /* Overlapping double-map rejected. */
    uint64_t dev2=0;
    assert(pci_dma_map(buf.pages[0],4096u,RIX_DMA_FROM_DEVICE,0xB2u,&dev2)!=0);
    assert(pci_dma_map(buf.pages[0],100u,RIX_DMA_TO_DEVICE,0xB2u,&dev2)!=0);
    printf("double-map-reject PASS\n");

    /* Bad inputs rejected. */
    assert(pci_dma_map(0,4096u,RIX_DMA_TO_DEVICE,1u,&dev2)!=0);
    assert(pci_dma_map(buf.pages[0],0u,RIX_DMA_TO_DEVICE,1u,&dev2)!=0);
    assert(pci_dma_map(buf.pages[0],4096u,99,1u,&dev2)!=0);
    assert(pci_dma_map(buf.pages[0],4096u,RIX_DMA_TO_DEVICE,0u,&dev2)!=0);
    assert(pci_dma_map(0xFFFFFFFFFFFFF000ULL,4096u,RIX_DMA_TO_DEVICE,1u,&dev2)!=0);
    printf("bad-arg-reject PASS\n");

    /* Unmap requires exact match. */
    assert(pci_dma_unmap(dev,100u)!=0);
    assert(pci_dma_unmap(dev+0x1000u,4096u)!=0);
    assert(pci_dma_unmap(dev,4096u)==0);
    assert(!pci_dma_is_mapped(buf.pages[0],4096u));
    assert(pci_dma_unmap(dev,4096u)!=0);
    printf("unmap PASS\n");

    /* SG maps each page, rolls back on failure. */
    uint64_t addrs[2];
    uint64_t list[2]={buf.pages[0],buf.pages[1]};
    assert(pci_dma_map_sg(list,2,RIX_DMA_BIDIRECTIONAL,0xC3u,addrs)==0);
    assert(addrs[0]==list[0]&&addrs[1]==list[1]);
    assert(pci_dma_is_mapped(list[1],4096u));
    assert(pci_dma_unmap(addrs[0],4096u)==0);
    assert(pci_dma_unmap(addrs[1],4096u)==0);
    printf("sg PASS\n");

    /* SG rollback: second entry invalid leaves first unmapped. */
    uint64_t bad[2]={buf.pages[0],0xFFFFFFFFFFFFF000ULL};
    assert(pci_dma_map_sg(bad,2,RIX_DMA_TO_DEVICE,0xD4u,addrs)!=0);
    assert(!pci_dma_is_mapped(buf.pages[0],4096u));
    printf("sg-rollback PASS\n");

    /* Sync is callable (ordering only). */
    pci_dma_sync_for_device(buf.pages[0],4096u);
    pci_dma_sync_for_cpu(buf.pages[0],4096u);
    printf("sync PASS\n");

    /* Bounce: low buffer needs no bounce. */
    assert(!pci_dma_needs_bounce(buf.pages[0],4096u,0x100000000ULL));
    uint64_t bdev=0;
    assert(pci_dma_map_bounce(buf.pages[0],512u,RIX_DMA_TO_DEVICE,0xE5u,0x100000000ULL,&bdev)==0);
    assert(bdev==buf.pages[0]);
    assert(pci_dma_unmap_bounce(bdev,512u,RIX_DMA_TO_DEVICE)==0);
    printf("bounce-passthrough PASS\n");

    /* DMA-after-free is contained: free skips still-mapped pages. */
    assert(pci_dma_map(buf.pages[1],4096u,RIX_DMA_FROM_DEVICE,0xF6u,&dev)==0);
    rix_dma_buffer_t alias; alias.count=1; alias.pages[0]=buf.pages[1];
    pci_dma_free(&alias);
    assert(pci_dma_is_mapped(buf.pages[1],4096u));
    assert(pci_dma_unmap(dev,4096u)==0);
    pmm_free_page(buf.pages[1]);
    pmm_free_page(buf.pages[0]);
    printf("after-free-contain PASS\n");

    pci_dma_free(&buf);
    assert(lockdep_warnings==0u);
    printf("dma tests: PASS\n");
    return 0;
}
