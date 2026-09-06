#include "power.h"
#include <stdint.h>

static inline void outb(uint16_t port,uint8_t value){__asm__ volatile("outb %0,%1"::"a"(value),"Nd"(port));}
void power_halt(void){__asm__ volatile("cli");for(;;)__asm__ volatile("hlt");}
int power_reboot(void){
    __asm__ volatile("cli");
    outb(0xCF9u,0x02u);
    outb(0xCF9u,0x06u);
    for(volatile uint32_t i=0;i<1000000u;i++)__asm__ volatile("pause");
    __asm__ volatile("int $0x19");
    return -1;
}
int power_shutdown(void){return -1;}
