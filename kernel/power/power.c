#include "power.h"
#include "../arch/x86_64/acpi.h"
#include <stdint.h>
static inline void outb(uint16_t port,uint8_t value){__asm__ volatile("outb %0,%1"::"a"(value),"Nd"(port));}
static inline void outw(uint16_t port,uint16_t value){__asm__ volatile("outw %0,%1"::"a"(value),"Nd"(port));}
void power_halt(void){__asm__ volatile("cli");for(;;)__asm__ volatile("hlt");}
int power_reboot(void){__asm__ volatile("cli");outb(0xCF9u,0x02u);outb(0xCF9u,0x06u);for(volatile uint32_t i=0;i<1000000u;i++)__asm__ volatile("pause");__asm__ volatile("int $0x19");return -1;}
int power_shutdown(void){acpi_power_info_t p;if(acpi_power_info(&p)!=0||!p.available||!p.pm1a_control)return-1;outw(p.pm1a_control,(uint16_t)(p.sleep_type_a|0x2000u));if(p.pm1b_control)outw(p.pm1b_control,(uint16_t)(p.sleep_type_b|0x2000u));for(volatile uint32_t i=0;i<1000000u;i++)__asm__ volatile("pause");return-1;}
