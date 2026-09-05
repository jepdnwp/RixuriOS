#pragma once
#include <stddef.h>
#include <stdint.h>

int user_range_valid(uint64_t address,size_t length,int write);
int copy_from_user(void *kernel_dst,uint64_t user_src,size_t length);
int copy_to_user(uint64_t user_dst,const void *kernel_src,size_t length);
