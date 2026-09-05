#include "dma.h"
#include "../mm/pmm.h"
#include <stddef.h>

int pci_dma_alloc(size_t pages,uint64_t max_physical_exclusive,rix_dma_buffer_t*out){
    if(!out||!pages||pages>RIX_DMA_MAX_PAGES||max_physical_exclusive<=RIXURI_PAGE_SIZE)return -1;
    out->count=0;
    for(size_t i=0;i<pages;i++){uint64_t pa=pmm_alloc_page_below(max_physical_exclusive);if(!pa){pci_dma_free(out);return -1;}out->pages[out->count++]=pa;}
    return 0;
}
void pci_dma_free(rix_dma_buffer_t*b){if(!b)return;for(size_t i=0;i<b->count;i++)if(b->pages[i])pmm_free_page(b->pages[i]);b->count=0;}
