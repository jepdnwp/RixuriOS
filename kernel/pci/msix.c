#include "msix.h"
#include "../mm/vmm.h"
#include <stddef.h>

#define PCI_CAP_ID_MSIX 0x11u
#define MSIX_CTRL_ENABLE (1u << 15)
#define MSIX_CTRL_MASKALL (1u << 14)
#define MSIX_ENTRY_SIZE 16u
#define MSIX_ENTRY_MASKED 1u

static int capability(const rix_pci_device_t *d, uint8_t *cap){
    if(!d||!cap)return -1;
    return pci_find_capability(d,PCI_CAP_ID_MSIX,cap);
}
static int table_location(const rix_pci_device_t*d,uint16_t*count,uint8_t*bir,uint32_t*off,uint8_t*cap){
    if(capability(d,cap)!=0)return -1;
    uint32_t c=pci_config_read32(d->bus,d->device,d->function,(uint8_t)(*cap&0xfcu));
    if(((c>>16)&0xffu)!=PCI_CAP_ID_MSIX)return -2;
    uint32_t table=pci_config_read32(d->bus,d->device,d->function,(uint8_t)((*cap+4u)&0xfcu));
    *count=(uint16_t)((c>>16)&0x7ffu)+1u;
    *bir=(uint8_t)(table&7u);*off=table&~7u;
    return *count&&*count<=RIX_MSIX_MAX_VECTORS?0:-3;
}
static int table_base(const rix_pci_device_t*d,uint8_t bir,uint32_t off,uint64_t*base){
    if(bir>=6||!base)return -1;
    uint64_t size=0,b=0;int io=0;
    if(pci_bar_size(d,bir,&size,&b,&io)!=0||io||!size)return -2;
    if((uint64_t)off>=size||size-(uint64_t)off<MSIX_ENTRY_SIZE)return -3;
    uint64_t page=(b+(uint64_t)off)&~0xfffULL;
    if(vmm_map_page(page,page,RIXURI_PTE_PRESENT|RIXURI_PTE_WRITE|RIXURI_PTE_NX)!=0)return -4;
    *base=page+(b+(uint64_t)off-page);
    return 0;
}
int pci_msix_set_entry(const rix_pci_device_t*d,unsigned vector,uint64_t address,uint32_t data,int masked){
    uint16_t n;uint8_t bir,cap;uint32_t off;if(table_location(d,&n,&bir,&off,&cap)!=0||vector>=n)return -1;
    uint64_t base;if(table_base(d,bir,off+(uint32_t)vector*MSIX_ENTRY_SIZE,&base)!=0)return -2;
    volatile uint32_t*p=(volatile uint32_t*)(uintptr_t)base;
    p[0]=(uint32_t)address;p[1]=(uint32_t)(address>>32);p[2]=data;p[3]=masked?MSIX_ENTRY_MASKED:0u;
    return 0;
}
int pci_msix_enable(const rix_pci_device_t*d,unsigned vector){
    uint16_t n;uint8_t bir,cap;uint32_t off;if(table_location(d,&n,&bir,&off,&cap)!=0||vector>=n)return -1;
    uint64_t address=0xfee00000ULL;
    if(pci_msix_set_entry(d,vector,address,vector,0)!=0)return -2;
    uint8_t a=(uint8_t)(cap&0xfcu);uint32_t c=pci_config_read32(d->bus,d->device,d->function,a);c|=MSIX_CTRL_ENABLE;c&=~MSIX_CTRL_MASKALL;
    return pci_config_write32(d->bus,d->device,d->function,a,c);
}
int pci_msix_disable(const rix_pci_device_t*d){
    uint8_t cap;if(capability(d,&cap)!=0)return -1;uint8_t a=(uint8_t)(cap&0xfcu);uint32_t c=pci_config_read32(d->bus,d->device,d->function,a);c&=~MSIX_CTRL_ENABLE;return pci_config_write32(d->bus,d->device,d->function,a,c);
}
