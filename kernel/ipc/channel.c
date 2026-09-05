#include "channel.h"
#include <stdint.h>
static void copy_in(uint8_t*d,const uint8_t*s,size_t n){for(size_t i=0;i<n;i++)d[i]=s[i];}
void ipc_channel_init(rix_ipc_channel_t*c){if(!c)return;c->head=c->tail=c->count=0;c->closed=0;}
int ipc_write(rix_ipc_channel_t*c,const void*buf,size_t n,size_t*w){if(w)*w=0;if(!c||(!buf&&n))return -1;if(c->closed)return -2;size_t m=n;if(m>RIX_IPC_CAPACITY-c->count)m=RIX_IPC_CAPACITY-c->count;const uint8_t*s=(const uint8_t*)buf;size_t a=m;if(a>RIX_IPC_CAPACITY-c->tail)a=RIX_IPC_CAPACITY-c->tail;copy_in(c->data+c->tail,s,a);if(m>a)copy_in(c->data,s+a,m-a);c->tail=(c->tail+m)%RIX_IPC_CAPACITY;c->count+=m;if(w)*w=m;return m==n?0:-3;}
int ipc_read(rix_ipc_channel_t*c,void*buf,size_t n,size_t*r){if(r)*r=0;if(!c||(!buf&&n))return -1;size_t m=n;if(m>c->count)m=c->count;uint8_t*d=(uint8_t*)buf;size_t a=m;if(a>RIX_IPC_CAPACITY-c->head)a=RIX_IPC_CAPACITY-c->head;copy_in(d,c->data+c->head,a);if(m>a)copy_in(d+a,c->data,m-a);c->head=(c->head+m)%RIX_IPC_CAPACITY;c->count-=m;if(r)*r=m;if(!m&&c->closed)return -2;return m==n?0:-3;}
int ipc_close(rix_ipc_channel_t*c){if(!c)return -1;c->closed=1;return 0;}
