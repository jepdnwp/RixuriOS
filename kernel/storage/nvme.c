#include "nvme.h"
#include "../pci/pci.h"
#include "../mm/vmm.h"
#include <stddef.h>

#define NVME_MAX_CONTROLLERS 8
#define NVME_CLASS 0x01
#define NVME_SUBCLASS 0x08
#define NVME_CAP 0x00
#define NVME_VS 0x08
#define NVME_CC 0x14
#define NVME_CSTS 0x1C
#define NVME_CC_EN (1u<<0)
#define NVME_CSTS_RDY (1u<<0)
#define NVME_BAR_MEM_MASK 0xFFFFFFF0u

static rix_nvme_controller_t controllers[NVME_MAX_CONTROLLERS];
static size_t count;

static volatile uint32_t *map_regs(uint64_t bar){uint64_t page=bar&~0xFFFULL;if(vmm_map_page(page,page,RIXURI_PTE_PRESENT|RIXURI_PTE_WRITE|RIXURI_PTE_NX)!=0)return NULL;return (volatile uint32_t *)(uintptr_t)page;}
static uint64_t bar_address(const rix_pci_device_t *d){uint32_t lo=d->bars[0];if(lo&1u)return 0;uint64_t base=(uint64_t)(lo&NVME_BAR_MEM_MASK);if(((lo>>1)&3u)==2u)base|=(uint64_t)d->bars[1]<<32;return base;}

int nvme_init(void){
    count=0;
    for(size_t i=0;i<pci_device_count()&&count<NVME_MAX_CONTROLLERS;i++){
        const rix_pci_device_t *d=pci_device(i);if(!d||d->class_code!=NVME_CLASS||d->subclass!=NVME_SUBCLASS)continue;
        uint64_t bar=bar_address(d);if(!bar)continue;volatile uint32_t *r=map_regs(bar);if(!r)continue;
        volatile uint64_t *cap=(volatile uint64_t *)(uintptr_t)((uint8_t*)r+(NVME_CAP&~0xFFFULL));
        uint64_t capv=*cap;uint32_t vs=r[NVME_VS/4];uint32_t cc=r[NVME_CC/4];uint32_t csts=r[NVME_CSTS/4];
        rix_nvme_controller_t *c=&controllers[count++];c->bus=d->bus;c->device=d->device;c->function=d->function;c->bar0=bar;c->cap=capv;c->version=vs;c->cc=cc;c->csts=csts;c->mqes=(uint16_t)(capv&0xffffu);c->dstrd=(uint8_t)((capv>>32)&0xfu);c->css=(uint8_t)((capv>>37)&0xffu);
        /* Do not set CC.EN until admin queues are programmed. */
        (void)NVME_CC_EN;(void)NVME_CSTS_RDY;
    }
    return 0;
}
size_t nvme_controller_count(void){return count;}
const rix_nvme_controller_t *nvme_controller(size_t index){return index<count?&controllers[index]:NULL;}
