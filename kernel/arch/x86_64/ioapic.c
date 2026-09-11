#include "ioapic.h"
#include "acpi.h"
#include "../../mm/vmm.h"
#include <stddef.h>

#define IOREGSEL 0x00
#define IOWIN 0x10
#define REDIR_BASE 0x10
#define MASK (1u<<16)
#define POLARITY_LOW (1u<<13)
#define TRIGGER_LEVEL (1u<<15)
#define IOAPIC_MAX 16u

typedef struct { volatile uint32_t *base; uint32_t gsi_base, max_redir; } ioapic_state_t;
static ioapic_state_t chips[IOAPIC_MAX];
static size_t chip_count;
static void wr(ioapic_state_t *c,uint32_t r,uint32_t v){if(!c||!c->base)return;c->base[IOREGSEL/4]=r;c->base[IOWIN/4]=v;}
static uint32_t rd(ioapic_state_t *c,uint32_t r){if(!c||!c->base)return 0;c->base[IOREGSEL/4]=r;return c->base[IOWIN/4];}
static ioapic_state_t *for_gsi(uint32_t gsi){for(size_t i=0;i<chip_count;i++){ioapic_state_t*c=&chips[i];if(gsi>=c->gsi_base&&gsi-c->gsi_base<c->max_redir)return c;}return NULL;}
int ioapic_init(void){
    chip_count=0;
    size_t n=acpi_ioapic_count();
    for(size_t i=0;i<n&&chip_count<IOAPIC_MAX;i++){
        const acpi_ioapic_info_t *info=acpi_ioapic(i);
        if(!info||!info->address)continue;
        uint64_t page=info->address&~0xfffULL;
        uint64_t vpage=vmm_map_mmio(page,0x1000ULL);
        if(!vpage)continue;
        ioapic_state_t*c=&chips[chip_count++];c->base=(volatile uint32_t*)(uintptr_t)(vpage+(info->address-page));c->gsi_base=info->gsi_base;
        c->max_redir=((rd(c,1)>>16)&0xffu)+1u;
        for(uint32_t r=0;r<c->max_redir;r++){wr(c,REDIR_BASE+r*2,MASK|0xff);wr(c,REDIR_BASE+r*2+1,0);}
    }
    return chip_count?0:-1;
}
static uint32_t acpi_route_flags(unsigned irq){uint16_t flags=0;uint32_t gsi=acpi_irq_gsi((uint8_t)irq,&flags);(void)gsi;uint32_t low=0;uint16_t polarity=(uint16_t)(flags&3u),trigger=(uint16_t)((flags>>2)&3u);if(polarity==3u)low|=POLARITY_LOW;else if(polarity!=0u&&polarity!=1u)return 0xffffffffu;if(trigger==3u)low|=TRIGGER_LEVEL;else if(trigger!=0u&&trigger!=1u)return 0xffffffffu;return low;}
static int entry(unsigned irq,uint8_t vec,uint32_t apic){uint32_t gsi=acpi_irq_gsi((uint8_t)irq,NULL),route=acpi_route_flags(irq);ioapic_state_t*c=for_gsi(gsi);if(!c||route==0xffffffffu)return -1;uint32_t r=REDIR_BASE+(gsi-c->gsi_base)*2;wr(c,r,(uint32_t)vec|route|MASK);wr(c,r+1,apic<<24);return 0;}
int ioapic_route_irq(unsigned irq,uint8_t vector,uint32_t apic_id){return entry(irq,vector,apic_id);}
void ioapic_mask_irq(unsigned irq){uint32_t g=acpi_irq_gsi((uint8_t)irq,NULL);ioapic_state_t*c=for_gsi(g);if(!c)return;uint32_t r=REDIR_BASE+(g-c->gsi_base)*2;wr(c,r,rd(c,r)|MASK);}
void ioapic_unmask_irq(unsigned irq){uint32_t g=acpi_irq_gsi((uint8_t)irq,NULL);ioapic_state_t*c=for_gsi(g);if(!c)return;uint32_t r=REDIR_BASE+(g-c->gsi_base)*2;wr(c,r,rd(c,r)&~MASK);}
