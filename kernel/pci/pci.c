#include "pci.h"
#include <stddef.h>

static rix_pci_device_t devices[RIX_PCI_MAX_DEVICES];
static size_t count;
static inline void outl(uint16_t p,uint32_t v){__asm__ volatile("outl %0,%1"::"a"(v),"Nd"(p));}
static inline uint32_t inl(uint16_t p){uint32_t v;__asm__ volatile("inl %1,%0":"=a"(v):"Nd"(p));return v;}

uint32_t pci_config_read32(uint8_t bus,uint8_t device,uint8_t function,uint8_t offset){if(device>=32||function>=8||(offset&3)||offset>=256)return 0xffffffffu;uint32_t a=0x80000000u|((uint32_t)bus<<16)|((uint32_t)device<<11)|((uint32_t)function<<8)|offset;outl(0xCF8,a);return inl(0xCFC);}
int pci_config_write32(uint8_t bus,uint8_t device,uint8_t function,uint8_t offset,uint32_t value){if(device>=32||function>=8||(offset&3)||offset>=256)return -1;uint32_t a=0x80000000u|((uint32_t)bus<<16)|((uint32_t)device<<11)|((uint32_t)function<<8)|offset;outl(0xCF8,a);outl(0xCFC,value);return 0;}

static void scan_function(uint8_t bus,uint8_t dev,uint8_t fn){
    uint32_t id=pci_config_read32(bus,dev,fn,0);if(id==0xffffffffu)return;if(count>=RIX_PCI_MAX_DEVICES)return;
    rix_pci_device_t *d=&devices[count++];d->bus=bus;d->device=dev;d->function=fn;d->vendor_id=(uint16_t)id;d->device_id=(uint16_t)(id>>16);
    uint32_t classreg=pci_config_read32(bus,dev,fn,8);d->revision=(uint8_t)classreg;d->prog_if=(uint8_t)(classreg>>8);d->subclass=(uint8_t)(classreg>>16);d->class_code=(uint8_t)(classreg>>24);
    d->header_type=(uint8_t)(pci_config_read32(bus,dev,fn,0xC)>>16);for(unsigned i=0;i<6;i++)d->bars[i]=pci_config_read32(bus,dev,fn,(uint8_t)(0x10+i*4));
}
int pci_init(void){count=0;for(uint16_t dev=0;dev<32&&count<RIX_PCI_MAX_DEVICES;dev++){uint32_t id=pci_config_read32(0,(uint8_t)dev,0,0);if(id==0xffffffffu)continue;scan_function(0,(uint8_t)dev,0);uint8_t hdr=(uint8_t)(pci_config_read32(0,(uint8_t)dev,0,0xC)>>16);if(hdr&0x80)for(uint8_t fn=1;fn<8&&count<RIX_PCI_MAX_DEVICES;fn++)scan_function(0,(uint8_t)dev,fn);}return 0;}
size_t pci_device_count(void){return count;}
const rix_pci_device_t *pci_device(size_t index){return index<count?&devices[index]:NULL;}
