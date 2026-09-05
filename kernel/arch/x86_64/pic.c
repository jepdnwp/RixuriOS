#include "pic.h"
#include <stdint.h>
static inline void outb(uint16_t p,uint8_t v){__asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p));}
void pic_disable(void){outb(0x21,0xff);outb(0xa1,0xff);}
