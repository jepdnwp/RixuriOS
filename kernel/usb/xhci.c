#include "xhci.h"
#include "../pci/pci.h"
#include "../mm/vmm.h"
#include <stddef.h>
#define XHCI_MAX 4
#define PCI_CLASS_SERIAL 0x0C
#define PCI_SUBCLASS_USB 0x03
#define PCI_PROGIF_XHCI 0x30
#define XHCI_CAPLENGTH 0x00
#define XHCI_HCIVERSION 0x02
#define XHCI_HCSPARAMS1 0x04
#define XHCI_HCCPARAMS1 0x10
#define XHCI_USBCMD 0x80
#define XHCI_USBSTS 0x84
#define XHCI_CMD_RS (1u<<0)
#define XHCI_STS_HCH (1u<<0)
static rix_xhci_controller_t controllers[XHCI_MAX];static size_t count;
static volatile uint8_t *map_regs(uint64_t bar){uint64_t page=bar&~0xFFFULL;if(vmm_map_page(page,page,RIXURI_PTE_PRESENT|RIXURI_PTE_WRITE|RIXURI_PTE_NX)!=0)return NULL;return(volatile uint8_t *)(uintptr_t)page;}
int xhci_init(void){count=0;for(size_t i=0;i<pci_device_count()&&count<XHCI_MAX;i++){const rix_pci_device_t*d=pci_device(i);if(!d||d->class_code!=PCI_CLASS_SERIAL||d->subclass!=PCI_SUBCLASS_USB||d->prog_if!=PCI_PROGIF_XHCI)continue;uint32_t lo=d->bars[0];if(lo&1u)continue;uint64_t bar=(uint64_t)(lo&0xfffffff0u);if(((lo>>1)&3u)==2u)bar|=(uint64_t)d->bars[1]<<32;volatile uint8_t*r=map_regs(bar);if(!r)continue;rix_xhci_controller_t*c=&controllers[count++];c->bus=d->bus;c->device=d->device;c->function=d->function;c->bar0=bar;c->cap_length=r[XHCI_CAPLENGTH];c->hci_version=*(volatile uint16_t *)(r+XHCI_HCIVERSION);uint32_t hcs=*(volatile uint32_t *)(r+XHCI_HCSPARAMS1);c->max_slots=(uint8_t)(hcs&0xffu);c->max_intrs=(uint8_t)((hcs>>8)&0x7ffu);c->max_ports=(uint8_t)((hcs>>24)&0xffu);c->hcc_params1=*(volatile uint32_t *)(r+XHCI_HCCPARAMS1);volatile uint32_t*op=(volatile uint32_t *)(r+c->cap_length);c->usbcmd=op[XHCI_USBCMD/4];c->usbsts=op[XHCI_USBSTS/4];(void)XHCI_CMD_RS;(void)XHCI_STS_HCH; }return 0;}
size_t xhci_controller_count(void){return count;}const rix_xhci_controller_t*xhci_controller(size_t index){return index<count?&controllers[index]:NULL;}
