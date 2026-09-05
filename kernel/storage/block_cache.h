#pragma once
#include "block.h"

int block_cache_init(void);
int block_cache_read(rix_block_device_t *device,uint64_t sector,void *buffer);
int block_cache_write(rix_block_device_t *device,uint64_t sector,const void *buffer);
int block_cache_flush(rix_block_device_t *device);
