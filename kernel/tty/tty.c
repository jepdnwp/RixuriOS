#include "tty.h"
#include <stdint.h>
static rix_tty_t ttys[RIX_TTY_COUNT];
void tty_init(void){for(unsigned i=0;i<RIX_TTY_COUNT;i++){ttys[i].head=ttys[i].tail=ttys[i].count=0;ttys[i].canonical=1;ttys[i].echo=1;ttys[i].foreground_pgrp=0;}}
rix_tty_t*tty_get(unsigned id){return id<RIX_TTY_COUNT?&ttys[id]:0;}
int tty_input(unsigned id,uint8_t ch){rix_tty_t*t=tty_get(id);if(!t)return -1;if(t->count==RIX_TTY_INPUT)return -2;if(ch==8||ch==127){if(t->count){t->tail=(t->tail+RIX_TTY_INPUT-1)%RIX_TTY_INPUT;t->count--;}return 0;}t->input[t->tail]=ch;t->tail=(t->tail+1)%RIX_TTY_INPUT;t->count++;return 0;}
int tty_read(unsigned id,void*buf,size_t n,size_t*out){if(out)*out=0;rix_tty_t*t=tty_get(id);if(!t||(!buf&&n))return -1;uint8_t*d=(uint8_t*)buf;size_t m=0;while(m<n&&t->count){uint8_t ch=t->input[t->head];if(t->canonical&&ch=='\n'&&m+1>n)break;d[m++]=ch;t->head=(t->head+1)%RIX_TTY_INPUT;t->count--;if(t->canonical&&ch=='\n')break;}if(out)*out=m;return m?0:-3;}
int tty_set_canonical(unsigned id,int e){rix_tty_t*t=tty_get(id);if(!t)return -1;t->canonical=e?1:0;return 0;}
int tty_set_echo(unsigned id,int e){rix_tty_t*t=tty_get(id);if(!t)return -1;t->echo=e?1:0;return 0;}
