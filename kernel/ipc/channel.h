#pragma once
#include <stddef.h>
#include <stdint.h>
#define RIX_IPC_CAPACITY 4096
typedef struct {uint8_t data[RIX_IPC_CAPACITY];size_t head,tail,count;uint32_t closed;} rix_ipc_channel_t;
void ipc_channel_init(rix_ipc_channel_t *c);
int ipc_write(rix_ipc_channel_t *c,const void *buf,size_t n,size_t *written);
int ipc_read(rix_ipc_channel_t *c,void *buf,size_t n,size_t *readn);
int ipc_close(rix_ipc_channel_t *c);
