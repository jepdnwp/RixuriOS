#include "kernel.h"
#include "tty/tty.h"
#include "sync/lock.h"
#define COM1 0x3F8u
static inline void outb(uint16_t port,uint8_t value){__asm__ volatile("outb %0,%1"::"a"(value),"Nd"(port));}
static inline uint8_t inb(uint16_t port){uint8_t value;__asm__ volatile("inb %1,%0":"=a"(value):"Nd"(port));return value;}
static uint8_t serial_has_com1;
static uint8_t serial_console_ready;
/* Phase E3: one console lock, always irqsave (fault/IRQ writers exist:
 * #PF/#DF/#GP print via kernel_log from IST stacks, ps2-IRQ echoes via
 * tty_output). Whole-call atomicity; per-line across calls is NOT
 * guaranteed (fragments stay intact, torn bytes disappear). Leaves only
 * under the lock (port IO, memory, FB MMIO): never sleep/yield/panic. */
static rix_spinlock_t console_lock;
static uint64_t console_lock_acquire(void){uint64_t f;rix_spin_lock_irqsave(&console_lock,&f);return f;}
static void console_lock_release(uint64_t f){rix_spin_unlock_irqrestore(&console_lock,f);}
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
 uint64_t f=console_lock_acquire();
 const char *p=s;
 while(*p){if(serial_has_com1)serial_putc(*p);p++;}
 if(serial_console_ready){size_t w=0;tty_output_nolock(0,s,(size_t)(p-s),&w);}
 console_lock_release(f);
}
void serial_write_n(const char *s,size_t length){
 if(!s)return;
 uint64_t f=console_lock_acquire();
 for(size_t i=0;i<length;i++)if(serial_has_com1)serial_putc(s[i]);
 if(serial_console_ready){size_t w=0;tty_output_nolock(0,s,length,&w);}
 console_lock_release(f);
}
/* COM1-only path: bytes go to the UART wire without touching the
 * framebuffer console. Use this when the caller already rendered to the
 * TTY (e.g. syscall write, serial input echo) so output is not doubled. */
void serial_write_com1(const char *s){
 if(!s||!serial_has_com1)return;
 uint64_t f=console_lock_acquire();
 while(*s)serial_putc(*s++);
 console_lock_release(f);
}
void serial_write_com1_n(const char *s,size_t length){
 if(!s||!serial_has_com1)return;
 uint64_t f=console_lock_acquire();
 for(size_t i=0;i<length;i++)serial_putc(s[i]);
 console_lock_release(f);
}
void serial_write_hex(uint64_t value){static const char digits[]="0123456789abcdef";char buf[19];buf[0]='0';buf[1]='x';for(int i=0;i<16;i++)buf[2+i]=digits[(value>>(60-4*i))&0xFULL];buf[18]=0;serial_write(buf);}
void serial_write_dec(uint64_t value){char buf[21];size_t i=sizeof(buf)-1;buf[i]=0;if(value==0){serial_write("0");return;}while(value){buf[--i]=(char)('0'+value%10ULL);value/=10ULL;}serial_write(&buf[i]);}
int serial_read_byte(uint8_t *byte){
 if(!serial_has_com1||!byte)return -1;
 uint64_t f=console_lock_acquire();
 int rc=-1;
 if((inb(COM1+5)&0x01u)!=0){*byte=inb(COM1);rc=0;}
 console_lock_release(f);
 return rc;
}
void serial_drain(void){if(!serial_has_com1)return;for(int i=0;i<4096;i++)if(inb(COM1+5)&0x40u)return;}

/* Use the single console path for diagnostics too. Before GOP/TTY is ready
 * this is COM1-only; after serial_console_enable() it is rendered on the
 * framebuffer exactly once as well, which is required on machines without a
 * physical COM1 connection. */
void kernel_log(const char *s){serial_write(s);}
void kernel_log_n(const char *s,size_t n){serial_write_n(s,n);}
void kernel_log_hex(uint64_t v){char buf[20];static const char d[]="0123456789abcdef";buf[0]='0';buf[1]='x';for(int i=2;i<18;i++)buf[i]=d[(v>>(60-((i-2)*4)))&0xFULL];buf[18]=0;kernel_log(buf);}
void kernel_log_dec(uint64_t v){char buf[21];size_t i=sizeof(buf)-1;buf[i]=0;if(v==0){kernel_log("0");return;}while(v){buf[--i]=(char)('0'+v%10ULL);v/=10ULL;}kernel_log(&buf[i]);}
void panic(const char *reason){kernel_log("RixuriOS PANIC: ");kernel_log(reason?reason:"unknown");kernel_log("\r\n");for(;;)__asm__ volatile("cli; hlt");}
