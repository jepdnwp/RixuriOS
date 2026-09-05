#include "pci.h"
#include <stddef.h>
static rix_pci_device_t devices[RIX_PCI_MAX_DEVICES];static size_t count;
static inline void outl(uint16_t p,uint32_t v){__asm__ volatile("outl %0,%1"::"a"(v),"Nd"(p));}
static inline uint32_t inl(uint16_t p){uint32_t v;__asm__ volatile("inl %1,%0":"=a"(v):"Nd"(p));return v;}
uint32_t pci_config_read32(uint8_t b,uint8_t d,uint8_t f,uint8_t o){if(d>=32||f>=8||(o&3)||o>=256)return 0xffffffffu;uint32_t a=0x80000000u|((uint32_t)b<<16)|((uint32_t)d<<11)|((uint32_t)f<<8)|o;outl(0xCF8,a);return inl(0xCFC);}
int pci_config_write32(uint8_t b,uint8_t d,uint8_t f,uint8_t o,uint32_t v){if(d>=32||f>=8||(o&3)||o>=256)return -1;uint32_t a=0x80000000u|((uint32_t)b<<16)|((uint32_t)d<<11)|((uint32_t)f<<8)|o;outl(0xCF8,a);outl(0xCFC,v);return 0;}
static void scan_function(uint8_t b,uint8_t d,uint8_t f){if(count>=RIX_PCI_MAX_DEVICES)return;uint32_t id=pci_config_read32(b,d,f,0);if(id==0xffffffffu)return;rix_pci_device_t*x=&devices[count++];x->bus=b;x->device=d;x->function=f;x->vendor_id=(uint16_t)id;x->device_id=(uint16_t)(id>>16);uint32_t c=pci_config_read32(b,d,f,8);x->revision=(uint8_t)c;x->prog_if=(uint8_t)(c>>8);x->subclass=(uint8_t)(c>>16);x->class_code=(uint8_t)(c>>24);x->header_type=(uint8_t)(pci_config_read32(b,d,f,0xC)>>16);for(unsigned i=0;i<6;i++)x->bars[i]=pci_config_read32(b,d,f,(uint8_t)(0x10+i*4));}
int pci_init(void){count=0;for(unsigned b=0;b<256&&count<RIX_PCI_MAX_DEVICES;b++)for(unsigned d=0;d<32&&count<RIX_PCI_MAX_DEVICES;d++){uint32_t id=pci_config_read32((uint8_t)b,(uint8_t)d,0,0);if(id==0xffffffffu)continue;scan_function((uint8_t)b,(uint8_t)d,0);uint8_t hdr=(uint8_t)(pci_config_read32((uint8_t)b,(uint8_t)d,0,0xC)>>16);if(hdr&0x80)for(uint8_t f=1;f<8&&count<RIX_PCI_MAX_DEVICES;f++)scan_function((uint8_t)b,(uint8_t)d,f);}return 0;}
size_t pci_device_count(void){return count;}const rix_pci_device_t*pci_device(size_t i){return i<count?&devices[i]:NULL;}
