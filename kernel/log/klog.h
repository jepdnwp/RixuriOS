#pragma once
#include <stddef.h>
#include <stdint.h>

#define KLOG_SIZE 65536u

void klog_push(const char *data, size_t length);
uint64_t klog_write_seq(void);
uint64_t klog_oldest(void);
size_t klog_copy(uint64_t start, uint8_t *dst, size_t count);
uint64_t klog_dropped(void);
