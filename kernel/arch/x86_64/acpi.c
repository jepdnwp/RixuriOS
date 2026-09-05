#include "acpi.h"
#include <stddef.h>
#include <stdint.h>

#define MAX_TABLE 16

typedef struct __attribute__((packed)) { char sig[8]; uint8_t checksum; char oem[6]; uint8_t revision; uint32_t rsdt; uint32_t length; uint64_t xsdt; uint8_t ext_checksum; uint8_t reserved[3]; } rsdp_t;
typedef struct __attribute__((packed)) { char sig[4]; uint32_t length; uint8_t revision; uint8_t checksum; char oemid[6]; char oemtable[8]; uint32_t oemrev; uint32_t creator; uint32_t creatorrev; } sdt_t;
typedef struct __attribute__((packed)) { uint8_t type; uint8_t length; } madt_hdr_t;
typedef struct __attribute__((packed)) { sdt_t h; uint32_t lapic; uint32_t flags; } madt_t;
typedef struct __attribute__((packed)) { uint8_t type, length, acpi_id, apic_id; uint32_t flags; } madt_lapic_t;
typedef struct __attribute__((packed)) { uint8_t type, length, id, reserved; uint32_t address, gsi_base; } madt_ioapic_t;
typedef struct __attribute__((packed)) { uint8_t type, length, bus, source; uint32_t gsi; uint16_t flags; } madt_iso_t;
typedef struct __attribute__((packed)) { uint8_t type, length; uint16_t reserved; uint32_t x2apic_id, flags, acpi_uid; } madt_x2apic_t;
typedef struct __attribute__((packed)) { uint64_t base; uint16_t segment; uint8_t start_bus; uint8_t end_bus; uint32_t reserved; } mcfg_entry_t;
typedef struct __attribute__((packed)) { char sig[4]; uint32_t length; uint8_t revision; uint8_t checksum; char rest[4]; } generic_sdt_t;
static acpi_cpu_info_t cpus[ACPI_MAX_CPUS];
static acpi_ioapic_info_t ioapics[ACPI_MAX_IOAPICS];
static acpi_mcfg_info_t mcfgs[ACPI_MAX_MCFG];
static size_t cpu_n,ioapic_n,mcfg_n;
static struct { uint8_t source; uint32_t gsi; uint16_t flags; } iso[16]; static size_t iso_n;
static uint8_t sum8(const void *p,size_t n){const uint8_t*b=p,s=0;while(n--)s=(uint8_t)(s+*b++);return s;}
static int sig4(const sdt_t*t,const char*s){return t&&t->sig[0]==s[0]&&t->sig[1]==s[1]&&t->sig[2]==s[2]&&t->sig[3]==s[3];}
static sdt_t*find_table(const rsdp_t*r,const char*sig){if(r->revision>=2&&r->xsdt){sdt_t*x=(sdt_t*)(uintptr_t)r->xsdt;if(x->length>=sizeof(sdt_t)&&sum8(x,x->length)==0){size_t n=(x->length-sizeof(sdt_t))/8;uint64_t*e=(uint64_t*)((uint8_t*)x+sizeof(sdt_t));for(size_t i=0;i<n&&i<MAX_TABLE;i++){sdt_t*t=(sdt_t*)(uintptr_t)e[i];if(t&&sig4(t,sig)&&t->length>=sizeof(sdt_t)&&sum8(t,t->length)==0)return t;}}}if(r->rsdt){sdt_t*x=(sdt_t*)(uintptr_t)(uint64_t)r->rsdt;if(x->length>=sizeof(sdt_t)&&sum8(x,x->length)==0){size_t n=(x->length-sizeof(sdt_t))/4;uint32_t*e=(uint32_t*)((uint8_t*)x+sizeof(sdt_t));for(size_t i=0;i<n&&i<MAX_TABLE;i++){sdt_t*t=(sdt_t*)(uintptr_t)(uint64_t)e[i];if(t&&sig4(t,sig)&&t->length>=sizeof(sdt_t)&&sum8(t,t->length)==0)return t;}}}return NULL;}
int acpi_init(uint64_t rsdp_phys){cpu_n=ioapic_n=mcfg_n=iso_n=0;if(!rsdp_phys)return -1;rsdp_t*r=(rsdp_t*)(uintptr_t)rsdp_phys;if(r->sig[0]!='R'||r->sig[1]!='S'||r->sig[2]!='D'||r->sig[3]!=' '||r->sig[4]!='P'||r->sig[5]!='T'||r->sig[6]!='R'||r->sig[7]!=' ')return -1;size_t len=20;if(r->revision>=2){if(r->length<sizeof(rsdp_t)||r->length>4096)return -1;len=r->length;}if(sum8(r,len)!=0)return -1;sdt_t*t=find_table(r,"APIC");if(!t||t->length<sizeof(madt_t)||sum8(t,t->length)!=0)return -1;madt_t*m=(madt_t*)t;uint8_t*p=(uint8_t*)m+sizeof(madt_t),*end=(uint8_t*)m+m->h.length;while(p+2<=end){madt_hdr_t*h=(madt_hdr_t*)p;if(h->length<2||p+h->length>end)return -2;if(h->type==0&&h->length>=sizeof(madt_lapic_t)&&cpu_n<ACPI_MAX_CPUS){madt_lapic_t*x=(madt_lapic_t*)p;cpus[cpu_n++]=(acpi_cpu_info_t){x->acpi_id,(uint8_t)(x->flags&1),0,x->apic_id};}else if(h->type==1&&h->length>=sizeof(madt_ioapic_t)&&ioapic_n<ACPI_MAX_IOAPICS){madt_ioapic_t*x=(madt_ioapic_t*)p;ioapics[ioapic_n++]=(acpi_ioapic_info_t){x->id,x->address,x->gsi_base};}else if(h->type==2&&h->length>=sizeof(madt_iso_t)&&iso_n<16){madt_iso_t*x=(madt_iso_t*)p;iso[iso_n].source=x->source;iso[iso_n].gsi=x->gsi;iso[iso_n++].flags=x->flags;}else if(h->type==9&&h->length>=sizeof(madt_x2apic_t)&&cpu_n<ACPI_MAX_CPUS){madt_x2apic_t*x=(madt_x2apic_t*)p;cpus[cpu_n++]=(acpi_cpu_info_t){0,(uint8_t)(x->flags&1),1,x->x2apic_id};}p+=h->length;}sdt_t*mcfg=find_table(r,"MCFG");if(mcfg&&mcfg->length>=sizeof(generic_sdt_t)+8){size_t n=(mcfg->length-sizeof(generic_sdt_t))/sizeof(mcfg_entry_t);mcfg_entry_t*e=(mcfg_entry_t*)((uint8_t*)mcfg+sizeof(generic_sdt_t));for(size_t i=0;i<n&&mcfg_n<ACPI_MAX_MCFG;i++)if(e[i].end_bus>=e[i].start_bus&&e[i].base)mcfgs[mcfg_n++]=(acpi_mcfg_info_t){e[i].base,e[i].segment,e[i].start_bus,e[i].end_bus};}return 0;}
size_t acpi_cpu_count(void){return cpu_n;}size_t acpi_ioapic_count(void){return ioapic_n;}size_t acpi_mcfg_count(void){return mcfg_n;}
const acpi_cpu_info_t*acpi_cpu(size_t i){return i<cpu_n?&cpus[i]:NULL;}const acpi_ioapic_info_t*acpi_ioapic(size_t i){return i<ioapic_n?&ioapics[i]:NULL;}const acpi_mcfg_info_t*acpi_mcfg(size_t i){return i<mcfg_n?&mcfgs[i]:NULL;}
uint32_t acpi_irq_gsi(uint8_t irq,uint16_t*flags){for(size_t i=0;i<iso_n;i++)if(iso[i].source==irq){if(flags)*flags=iso[i].flags;return iso[i].gsi;}if(flags)*flags=0;return irq;}
