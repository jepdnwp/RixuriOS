#include "pic.h"
#include <stdint.h>
static inline void outb(uint16_t p,uint8_t v){__asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p));}
static uint8_t active;
int pic_init(void){
 outb(0x20,0x11);outb(0xa0,0x11);
 outb(0x21,0x20);outb(0xa1,0x28);
 outb(0x21,0x04);outb(0xa1,0x02);
 outb(0x21,0x01);outb(0xa1,0x01);
 outb(0x21,0xfc);outb(0xa1,0xff);
 active=1;return 0;
}
void pic_disable(void){outb(0x21,0xff);outb(0xa1,0xff);active=0;}
void pic_eoi(unsigned irq){if(!active)return;if(irq>=8)outb(0xa0,0x20);outb(0x20,0x20);}
int pic_active(void){return active!=0;}
