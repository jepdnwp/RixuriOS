#include "kernel.h"
#include "tty/tty.h"
#define COM1 0x3F8u
static inline void outb(uint16_t port,uint8_t value){__asm__ volatile("outb %0,%1"::"a"(value),"Nd"(port));}
static inline uint8_t inb(uint16_t port){uint8_t value;__asm__ volatile("inb %1,%0":"=a"(value):"Nd"(port));return value;}
void serial_init(void){outb(COM1+1,0x00);outb(COM1+3,0x80);outb(COM1+0,0x03);outb(COM1+1,0x00);outb(COM1+3,0x03);outb(COM1+2,0xC7);outb(COM1+4,0x0B);}
static void serial_putc(char c){while((inb(COM1+5)&0x20)==0){}outb(COM1,(uint8_t)c);}
void serial_write(const char *s){if(!s)return;while(*s)serial_putc(*s++);}
void serial_write_n(const char *s,size_t length){if(!s)return;for(size_t i=0;i<length;i++)serial_putc(s[i]);}
void serial_write_hex(uint64_t value){static const char digits[]="0123456789abcdef";serial_write("0x");for(int shift=60;shift>=0;shift-=4)serial_putc(digits[(value>>shift)&0xFULL]);}
void serial_write_dec(uint64_t value){char buf[21];size_t i=sizeof(buf);if(value==0){serial_putc('0');return;}while(value){buf[--i]=(char)('0'+value%10ULL);value/=10ULL;}serial_write(&buf[i]);}
int serial_read_byte(uint8_t *byte){if(!byte||(inb(COM1+5)&0x01u)==0)return -1;*byte=inb(COM1);return 0;}

static size_t klog_strlen(const char *s){size_t n=0;while(s[n])n++;return n;}
void kernel_log(const char *s){serial_write(s);size_t w=0;tty_output(0,s,klog_strlen(s),&w);}
void kernel_log_n(const char *s,size_t n){serial_write_n(s,n);size_t w=0;tty_output(0,s,n,&w);}
void kernel_log_hex(uint64_t v){char buf[20];static const char d[]="0123456789abcdef";buf[0]='0';buf[1]='x';for(int i=2;i<18;i++)buf[i]=d[(v>>(60-((i-2)*4)))&0xFULL];buf[18]=0;kernel_log(buf);}
void kernel_log_dec(uint64_t v){char buf[21];size_t i=sizeof(buf);if(v==0){kernel_log("0");return;}while(v){buf[--i]=(char)('0'+v%10ULL);v/=10ULL;}kernel_log(&buf[i]);}
void panic(const char *reason){kernel_log("RixuriOS PANIC: ");kernel_log(reason?reason:"unknown");kernel_log("\r\n");for(;;)__asm__ volatile("cli; hlt");}
