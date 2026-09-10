#include "kernel.h"
#include "tty/tty.h"
#define COM1 0x3F8u
static inline void outb(uint16_t port,uint8_t value){__asm__ volatile("outb %0,%1"::"a"(value),"Nd"(port));}
static inline uint8_t inb(uint16_t port){uint8_t value;__asm__ volatile("inb %1,%0":"=a"(value):"Nd"(port));return value;}
static uint8_t serial_has_com1;
static uint8_t serial_console_ready;
void serial_init(void){
 outb(COM1+1,0x00);outb(COM1+3,0x80);outb(COM1+0,0x03);
 outb(COM1+1,0x00);outb(COM1+3,0x03);outb(COM1+2,0xC7);outb(COM1+4,0x0B);
 /* Do not trust scratch-register loopback alone: some physical Super-I/O
  * implementations echo it while their transmitter is not usable. */
 outb(COM1+7,0xAA);
 uint8_t lsr=inb(COM1+5);
 if(lsr!=0xFFu&&(lsr&0x20u)&&inb(COM1+7)==0xAA){
  outb(COM1+7,0x55);
  if(inb(COM1+7)==0x55)serial_has_com1=1;
 }
}
void serial_console_enable(void){serial_console_ready=1;}
static void serial_putc(char c){
 if(!serial_has_com1)return;
 for(int i=0;i<4096;i++){
  if(inb(COM1+5)&0x20){outb(COM1,(uint8_t)c);return;}
 }
}
void serial_write(const char *s){
 if(!s)return;
 while(*s){
  if(serial_has_com1)serial_putc(*s);
  if(serial_console_ready){size_t w=0;tty_output(0,s,1,&w);}
  s++;
 }
}
void serial_write_n(const char *s,size_t length){
 if(!s)return;
 for(size_t i=0;i<length;i++){
  if(serial_has_com1)serial_putc(s[i]);
  if(serial_console_ready){size_t w=0;tty_output(0,s+i,1,&w);}
 }
}
static void serial_debug_write(const char *s){
 if(!s)return;
 while(*s){if(serial_has_com1)serial_putc(*s);s++;}
}
void serial_write_hex(uint64_t value){static const char digits[]="0123456789abcdef";char buf[19];buf[0]='0';buf[1]='x';for(int i=0;i<16;i++)buf[2+i]=digits[(value>>(60-4*i))&0xFULL];buf[18]=0;serial_write(buf);}
void serial_write_dec(uint64_t value){char buf[21];size_t i=sizeof(buf)-1;buf[i]=0;if(value==0){serial_write("0");return;}while(value){buf[--i]=(char)('0'+value%10ULL);value/=10ULL;}serial_write(&buf[i]);}
int serial_read_byte(uint8_t *byte){if(!serial_has_com1||!byte||(inb(COM1+5)&0x01u)==0)return -1;*byte=inb(COM1);return 0;}
void serial_drain(void){if(!serial_has_com1)return;for(int i=0;i<4096;i++)if(inb(COM1+5)&0x40u)return;}

/* Boot/diagnostic logs must not repaint a slow physical GOP framebuffer one
 * character at a time. User-visible terminal output still uses serial_write;
 * kernel diagnostics stay on COM1 and cannot make a healthy machine look
 * frozen. */
void kernel_log(const char *s){serial_debug_write(s);}
void kernel_log_n(const char *s,size_t n){if(!s)return;for(size_t i=0;i<n;i++)if(serial_has_com1)serial_putc(s[i]);}
void kernel_log_hex(uint64_t v){char buf[20];static const char d[]="0123456789abcdef";buf[0]='0';buf[1]='x';for(int i=2;i<18;i++)buf[i]=d[(v>>(60-((i-2)*4)))&0xFULL];buf[18]=0;kernel_log(buf);}
void kernel_log_dec(uint64_t v){char buf[21];size_t i=sizeof(buf)-1;buf[i]=0;if(v==0){kernel_log("0");return;}while(v){buf[--i]=(char)('0'+v%10ULL);v/=10ULL;}kernel_log(&buf[i]);}
void panic(const char *reason){kernel_log("RixuriOS PANIC: ");kernel_log(reason?reason:"unknown");kernel_log("\r\n");for(;;)__asm__ volatile("cli; hlt");}
