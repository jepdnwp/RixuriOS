#include "ioapic.h"
#include "acpi.h"
#include "../../mm/vmm.h"
#include <stddef.h>

#define IOREGSEL 0x00
#define IOWIN 0x10
#define REDIR_BASE 0x10
#define MASK (1u<<16)
static volatile uint32_t *base;
static uint32_t gsi_base;
static uint32_t max_redir;
static void wr(uint8_t r,uint32_t v){if(!base)return;base[IOREGSEL/4]=r;base[IOWIN/4]=v;}
static uint32_t rd(uint8_t r){if(!base)return 0;base[IOREGSEL/4]=r;return base[IOWIN/4];}
int ioapic_init(void){const acpi_ioapic_info_t*i=acpi_ioapic(0);if(!i||!i->address)return -1;if(vmm_map_page(i->address,i->address,RIXURI_PTE_PRESENT|RIXURI_PTE_WRITE|RIXURI_PTE_NX)!=0)return -1;base=(volatile uint32_t*)(uintptr_t)i->address;gsi_base=i->gsi_base;max_redir=((rd(1)>>16)&0xffu)+1u;for(uint32_t n=0;n<max_redir;n++) {wr((uint8_t)(REDIR_BASE+n*2),MASK|0xff);wr((uint8_t)(REDIR_BASE+n*2+1),0);}return 0;}
static int entry(unsigned irq,uint8_t vec,uint8_t apic){uint32_t gsi=acpi_irq_gsi((uint8_t)irq,NULL);if(!base||gsi<gsi_base||gsi-gsi_base>=max_redir)return -1;uint8_t r=(uint8_t)(REDIR_BASE+(gsi-gsi_base)*2);wr(r,(uint32_t)vec);wr((uint8_t)(r+1),(uint32_t)apic<<24);return 0;}
int ioapic_route_irq(unsigned irq,uint8_t vector,uint8_t apic_id){return entry(irq,vector,apic_id);}
void ioapic_mask_irq(unsigned irq){uint32_t g=acpi_irq_gsi((uint8_t)irq,NULL);if(!base||g<gsi_base||g-gsi_base>=max_redir)return;uint8_t r=(uint8_t)(REDIR_BASE+(g-gsi_base)*2);wr(r,rd(r)|MASK);}
void ioapic_unmask_irq(unsigned irq){uint32_t g=acpi_irq_gsi((uint8_t)irq,NULL);if(!base||g<gsi_base||g-gsi_base>=max_redir)return;uint8_t r=(uint8_t)(REDIR_BASE+(g-gsi_base)*2);wr(r,rd(r)&~MASK);}
